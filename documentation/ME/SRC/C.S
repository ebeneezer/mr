macro_file C;
/* ******************************************************************************
														MULTI-EDIT MACRO FILE

Name:		C

Description:  This macro file contains language support macros for C.

C_IND - Smart indent for C
CMTCH - Construct/brace matching for C
BUILDCMT - Builds a shell of a comment block for C
CSETX - Sets up the C specific global variable for template expansion
C_CLOSE_PAREN - Highlights to matching open paren when closing paren is entered.
C_CLOSE_BRACE - Sets the indenting correctly when a closing brace is entered.

							 (C) Copyright 1992 by American Cybernetics, Inc.
****************************************************************************** */

// **********************************************************************
// You can easily customize to the most common C styles by
// changing the value of the define c_style (defined after this comment).
// After you recompile, you will need to either exit Multi-Edit and restart,
// or set the global string "!C.Tmplt0" to NIL ("")
//
// c_style == 0    if (x) {
//                   statement;
//                 }
//
//         == 1    if (x)
//                 {
//                   statement;
//                 }
//
//         == 2    if (x)
//                 { statement;
//                 }
//
//         == 3    if (x)
//                   {
//									 statement;
//	                 }
// **********************************************************************

#define c_style 1

// If you want "*" automatically put in when hitting <ENTER> inside a
// /* style comment, then set auto_comment to 1
#define auto_comment 0


macro C_IND {
/* ******************************************************************************
																MULTI-EDIT MACRO

Name: C_IND

Description: Performs a smart indent each time the Carriage Return or Enter
key is pressed.  Is called by the macro CR.

Written by Todd M. Johnson and William M. Miller.

The algorithm used is to look at the line-ending characters (ignoring
comments, including C++ '//' comments, labels, and white space) for the
current and preceding lines, then decide whether and how much to change the
indentation from that of the current line.  In addition, if the CR was done
inside a (C-style, / * * /) comment, the "line of asterisks" style of comment
continuation is automatically supplied.

							 (C) Copyright 1992 by American Cybernetics, Inc.
****************************************************************************** */

	if(  (Insert_Mode == False)  ) {
/* If a new line is not being created, just go to the indent level that was
there before and get out.
10-03-90 TR */
		Cr;
		First_Word;
		Goto MAC_EXIT;
	}
	int outdent_flag,
					sig_char_found,
					in_comment,  /* 1 = '/*' comment, 2 = '//' comment */
					curr_ind,
					start_col,
					start_line,
					cur_char_line,
					curr_end_char,
					prev_end_char,
					in_struct,
					junk,
					match_brace, go_match_brace,
					jx,
					Indent_Number,
					OldRefresh = Refresh;

	char  orig_char ;

  // Number of times to indent after a line that does not have ';{}'
  // at the end and flag for outdenting after a closing brace
	switch ( c_style )
	{
		case 0 :
			Indent_Number = 2;
      Outdent_flag = False;
			break;

		case 1 :
		case 2 :
			Indent_Number = 0;
      Outdent_flag = False;
			break;

		case 3 :
			Indent_Number = 1;
      Outdent_flag = True;
      break;
	}

	/* If you want your end brace to line up with the first word of the
	 line that the opening brace is on, then set Match_Brace to TRUE */
	Match_Brace = TRUE;
	go_match_brace = FALSE;

	Messages = False;
	reg_exp_stat = true;
	refresh = false;
	mark_pos;

	/*  If we are before or at the first character of the first word of the line
		then leave the level the same for the line  */
	start_col = c_col;
	start_line = c_line;
	first_word;
	if(  (c_col >= start_col)  ) {
		set_indent_level;
		cr;
		goto mac_exit;
	}
	orig_char = cur_char;
/* If we are at a directive command (#include or #define), get out */
	if(  (orig_char == '#')  ) {
		mark_pos;
		first_word;
		if( cur_char == "#") {
			pop_mark;
			set_indent_Level;
			goto_col(start_col);
			cr;
			goto mac_exit;
		}
		goto_mark;
	}
	goto_col(start_col);

	call skip_c_noise;

/*  If the cursor is inside a comment, skip_c_noise leaves it at the opening
	'/'.  We move one character right, to align with the '*', set the indent
	level, do the carriage return, and insert a '* ' to form the line of
	asterisks continuation.  */

	if(  (in_comment == 1)  ) {
		right;
		set_indent_level;
		right;
		while(  NOT(at_eol) & (xpos(cur_char,' |9|255',1) != 0)  ) {
			right;
		}
		start_col = c_col;
		goto_mark;
		cr;

		if ( (in_comment == 1) && auto_comment )
			text('* ');

	/*  while c_col < start_col do
			text(' ');
		end;  */

		set_indent_level;
		goto mac_exit;
	}

/*  Here, we are at the top of the file, before any non-comment text.  We just
	put everything in column 1 in that case.  */

	if(  (not(sig_char_found))  ) {
		goto_col(1);
		set_indent_level;
		goto_mark;
		cr;
		goto mac_exit;
	}

/*  Here we've found the nearest non-noise character.  There are four that
	specifically influence indentation decisions, so instead of carrying around
	a big string variable, we just save the index into a string of those four
	characters.  In addition, we find the first non-blank character on the line
	to determine its indentation.  Furthermore, we check if that first character
	is a '}' (probably as a result of the '} name;' at the end of a structure,
	union, or enum definition) so we can know how much to indent later.  */

	cur_char_line = c_line;
	if ( 1 == (curr_end_char = pos(cur_char, ';{},')) )
		if ( Search_Bwd('@{', 1) )
			++curr_end_char;

	/* Special processing if we have a lone '{' on a line */
	jx = c_col;            /*  store the current column position  */
	first_word;
	if ( (c_col == jx) && (curr_end_char == 2) )
	{
		if ( 3 == c_style )
			curr_ind = 0;
		else
			curr_ind = 1;
		set_indent_level;
		goto do_curs;
	}

	set_indent_level;
	curr_ind = 0;
	start_col = c_col;
	in_struct = (cur_char == '}');


/*  Now we get the line ending for the preceding line.  */
	call skip_c_noisex;  /* no need to check to see if we're in a comment */
	if(  (sig_char_found)  ) {
		prev_end_char = pos(cur_char, ';{}');
	} else {
		prev_end_char = 1;    /*  treat like semicolon  */
	}


/*  This code is executed when the nearest line-ending character was a semicolon.
	 */

	if(  (curr_end_char == 1)  ) {     /*  semicolon  */
		if(  (prev_end_char == 0)  ) {   /* if continuation line then undent */
			curr_ind = curr_ind - indent_number;
		}
		if(  (outdent_flag & in_struct)  ) {
			curr_ind = curr_ind - 1;
		}
	} else {


/*  This code is executed if the nearest line-ending character was a left
	brace.
	 */
		if(  (curr_end_char == 2)  ) {         /*  left brace  */
			curr_ind = curr_ind + 1;
			if(  (prev_end_char == 0)  ) {
				curr_ind = curr_ind - indent_number;  /* if continuation line then undent */
			}
		} else {
			if(  (curr_end_char == 3)  ) {       /*  right brace  */
				if(  (outdent_flag)  ) {
					curr_ind = curr_ind - 1;
				} else {
					if(  (Match_Brace)  ) {
						if(  cur_char_line == start_line  ) {
							go_match_brace = TRUE;
						}
					}
				}
			} else {

/*  Commas cause no indentation (to support enum declarations).  If the line-
	ending character was none of the above, the new line will be a continuation
	line, indented Indent_Number times.  However, if the preceding line-ending
	character was not ';{}', we assume that the current line was already a
	continuation line and no further indentation is necessary.  */

				if(  ((curr_end_char != 4) & (prev_end_char != 0))  ) {
					curr_ind = curr_ind + Indent_Number;
				}

			}
		}
	}

do_curs:
/*  Here we put the cursor at the correct column position, we set the indent
	level, return to the original position, and perform the carriage return.  */

	jx = 0;

	goto_mark;
	cr;
	if(  curr_ind > 0  ) {
		while(  jx < curr_ind  ) {
			++jx;
			indent;
		}
	} else {
		while(  jx > curr_ind  ) {
			--jx;
			undent;
		}
	}

	set_indent_level;

	if(  (go_match_brace) & (orig_char == '}')  ) {
		UP;
		First_Word;
		mark_pos;
		RM( 'CMTCH' );
		FIRST_WORD;
		set_indent_level;
		goto_mark;
		first_word;
		while(  (c_col > indent_level)  ) {
			back_space;
		}
		down;
		make_message('} undenting occurred.');
	}

	goto mac_exit;


skip_c_noisex:
	if(  (c_line == 1) && (c_col == 1)  ) {
		sig_char_found = FALSE;
		goto exit_skip_c;
	}
	left;
	goto skip_c_noise1;

/*  This subroutine moves the cursor to the nearest preceding non-whitespace
	character.  If there is no such character (the cursor is at the top of the
	file), the variable sig_char_found will be FALSE; otherwise, it will be
	true.  The macro also sets the variable in_comment appropriately.  */

skip_c_noise:
	left;
	mark_pos;

//  First check if we are inside a comment.  We look for the nearest of '/*' or
//	'*/'; if we find the former, we are inside a comment.  In this case, we
//	leave the cursor at the beginning of the comment to establish the correct
//	indentation margin and return.
		jx = c_line;

// If we are using syntax highlighting, then is is much easier to know.

	if( keywords_str != "" )
	{
		int ic = 0, tx = c_col;
		if( at_eol ) {
			ic = (line_stat & 0x30) != 0;
		}
		else if( c_line > 1 )
		{
			up;
			ic = (( line_stat & 0x30 ) != 0);
			down;
			first_word;
			while( !at_eol && (c_col < tx ) )
			{
				if( ic == 0 ) {
					forward_till("/\"");
					if( c_col < tx )
					{
						if( cur_char == "\"" )
						{
							quote_again:
								right;
								forward_till("\\\"");
								if( (cur_char == "\\") && !at_eol )
								{
									right;
									goto quote_again;
								}
						}
						else if( cur_char == "/" )
						{
							right;
							if( cur_char == "*" )
							{
								ic = 1;
							}
						}
					}
				}
				else
				{
					forward_till("*");
					if( c_col < tx ) {
						if( cur_char == "*" )
						{
							right;
							if( cur_char == "/" )
							{
								ic = 0;
							}
						}
					}
				}
				right;
			}
			goto_col( tx );
		}
		if( !ic )
			goto comment_done;
	}


comment_again:
	if(  (search_bwd('/',50))  ) {
		if(  c_col > 1  ) {
			left;
			if(  cur_char == '/'  ) {
				in_comment = FALSE;
				if(  c_line == jx  ) {
					left;
					pop_mark;
				} else {
					goto_mark;
				}
				goto skip_c_noise1;
			}
			right;
		}
		right;
		if(  (cur_char == '/')  ) {
			in_comment = FALSE;
			if(  c_line == jx  ) {
				left;
				left;
				pop_mark;
			} else {
				goto_mark;
			}
			goto skip_c_noise1;
		}
		if(  (cur_char == '*')  ) {
			pop_mark;
			in_comment = 1;
			left;
			goto exit_skip_c;
		}
		left;
		left;
		if(  (cur_char != '*')  ) {
			goto comment_again;
		}
	}

comment_done:
	goto_mark;
	in_comment = FALSE;

/*  The following code is a loop which skips over comments (including C++ '//'
	comments) and labels, leaving the cursor on the nearest preceding non-noise
	character, if any.  */



skip_c_noise1:
	if(  (Xpos('#',get_Line,1) > 0)  ) {
		mark_pos;
		first_word;
		if(cur_char == "#" ) {
			pop_mark;
/* If at a compiler directive, get out */
			sig_char_found = FALSE;
			goto exit_skip_c;
		}
		goto_mark;
	}
	if(  (c_line == 1) & (c_col == 1)  ) {
		sig_char_found = FALSE;
		goto exit_skip_c;
	}
/*  If there is a '//' on the line, we move the cursor before it and
	look some more.  */

	if(  (search_bwd('//', 1))  ) {
		if(  ((c_col == 1) & (c_line == 1))  ) {
			sig_char_found = FALSE;
			goto exit_skip_c;
		}
		left;
		goto skip_c_noise1;
	}



/*  Here we look for the nearest preceding nonblank character.  If it is a '/',
	we check if the preceding character was an asterisk; if so, we search for
	the corresponding '/*' and try again.  If the character before the '/' was
	not '*', we have found a significant character and are done.  */

	if(  (search_bwd('[~|9 ]', 1))  ) {
		if(  (cur_char == '/')  ) {
			left;
			if(  (cur_char == '*')  ) {
				junk = search_bwd('/@*', 0);
				left;
				goto skip_c_noise1;
			}
			right;
			sig_char_found = TRUE;
			goto exit_skip_c;
		}

/*  If the character we found was a colon, we have found a label.  We assume
	that the label was the first thing on the line and proceed to the end of
	the preceding line.  Any other character is significant.  */

		if(  (cur_char == ':')  ) {
			if(  (C_Line == 1)  ) {
				Eol;
				cr;
				Goto_Col(1);
				Indent;
				Set_Indent_Level;
				goto mac_exit;
			}
			up;
			eol;
			goto skip_c_noise1;
		}
		sig_char_found = TRUE;
		goto exit_skip_c;
	}

/*  If we failed to find a nonblank character on the current line, and the
	cursor is on line 1, we failed to find a significant character; otherwise,
	we back up a line and try again.  */

	if(  (c_line == 1)  ) {
		sig_char_found = FALSE;
		goto exit_skip_c;
	}
	up;
	eol;
	goto skip_c_noise1;

exit_skip_c:
	ret;

mac_exit:
	Refresh = OldRefresh;
	Messages = True;
}

macro CMTCH TRANS {
// ******************************************************************************
//																MULTI-EDIT MACRO
//
// Name: CMTCH
//
// Description: Match occurances of /*  */ or () and handles problems with
//	characters embedded in comments or quotes.
//	 11/01/88 11:15am  Modified for greater speed.  Also, handles screen
//										updates much better.
//
//	 /HI=1		Highlight block
//	 /RC=1		Restore cursor to original position
//	 /LS=x		Limit scope of search to x number of lines.
//
//							 (C) Copyright 1992 by American Cybernetics, Inc.
// ****************************************************************************** */
	str  Str1[20],Str2[20],  Search_Str ;  /* Strings to match */

	int  Direction,         /* 1 = Search forward, 0 = backward */
					 B_Count,       /* Brace count.  0 = match found */
					 S_Res,         /* Result of last search */
					 T_Row, T_Col, T_Line, /* Holds the original position */
					 JX,            /* General purpos */
					 F_Line, F_Col, /* Found position */
           tstream_block = stream_block_mode,
					 OldRefresh = Refresh,
					 shift_stat = peek( 0, 0x417 ),
					 scope = parse_int('/LS=', mparm_str),
					 cmac = (parse_str( '|127LS=', global_str( '.' + get_extension(file_name))) == 'CMAC' )
					 ;

	Refresh = False;      /* Turn screen display off */
	T_Line = C_Line;      /* Store the current position */
	T_Col = C_Col;
  stream_block_mode = 1;  // Make stream blocks inclusive

	Push_Undo;
	Mark_Pos;

Find_Match_Str:

	if(  (Cur_Char == '(')  ) {    /* If current char is a '(' then setup */
		Str1 = '(';
		Str2 = ')';
		Direction = 1;
		Goto Start_Match;
	}
	if(  (Cur_Char == ')')  ) {    /* If current char is a ')' then setup */
		Str1 = ')';
		Str2 = '(';
		Direction = 0;
		Goto Start_Match;
	}
	if(  (Cur_Char == '{')  ) {    /* If current char is a '{' then setup */
		Str1 = '{';
		Str2 = '}';
		Direction = 1;
		Goto Start_Match;
	}
	if(  (Cur_Char == '}')  ) {    /* If current char is a '}' then setup */
		Str1 = '}';
		Str2 = '{';
		Direction = 0;
		Goto Start_Match;
	}
	if(  (Cur_Char == '{')  ) {    /* If current char is a '{' then setup */
		Str1 = '{';
		Str2 = '}';
		Direction = 1;
		Goto Start_Match;
	}
	if(  (Cur_Char == '}')  ) {    /* If current char is a '}' then setup */
		Str1 = '}';
		Str2 = '{';
		Direction = 0;
		Goto Start_Match;
	}
															/* If nothing matched then  */
															/*   Return to original position */
	Goto_Line(T_Line);
	Goto_Col(T_Col);
	Goto_Mark;
	Make_Message('NOTHING to Match');
	Goto Macro_Exit;            /* Go exit */

Start_Match:

 /*  Search_Str := '{\["'']}||[''"]||{/@*}||{@*/}||[' + Str1 + Str2+']'; */

	Search_Str = '["''/' + Str1 + Str2 + ']';

	Reg_Exp_Stat = True;
	Ignore_Case = True;					/* Ignore the search case */
	B_Count = 1;								/* Brace count starts at 1 */
	S_Res = 1;									/* Init search result to true */
															/* Tell the user what we're matching */
	Make_Message('Matching "'+Str1+'"...');


MATCH_LOOP:     /* Main loop */

	if(  S_Res == 0  ) {           /* If the last search was a failure then exit */
		RM('MEERROR^Beeps /C=1');
		Goto Error_Exit;
	}

	if(  B_Count == 0  ) {         /* If brace count is zero then success */
		Goto Found_Exit;
	}

			/* Execute search based on direction */
			/*   We are going to search not only for the match brace or paren, but
				 also for comments and double and single quotes */
	if(  Direction == 1  ) {
		if( scope > 0 ) {
			if( c_line > (t_line + scope)) {
				goto Error_Exit;
			}
		}
		Right;
		while(  (NOT (At_EOL)) & ((Cur_Char == '|255') |
					(Cur_Char == '|9'))  ) {
			RIGHT;
		}
		S_Res = Search_Fwd( Search_Str, scope);
	} else {
		if( scope > 0 ) {
			if( c_line < (t_line - scope)) {
				goto Error_Exit;
			}
		}
		Left;
		while(  (((C_Line > 1) | (C_Col > 1)) & ((Cur_Char == '|255') |
					(Cur_Char == '|9')))  ) {
			Left;
		}
		S_Res = Search_Bwd( Search_Str , scope);
	}

										/* If the search result was a failure then exit */
	if(  S_Res == 0  ) {
		Goto Error_Exit;
	}




										/* If we found the original string then up the count */
	if(  Found_Str == STR1  ) {
		B_Count = B_Count + 1;
		Goto Match_Loop;
	}
										/* If we found the matching string then decrement the count */
	if(  Found_Str == STR2  ) {
		B_Count = B_Count - 1;
		Goto Match_Loop;
	}

										/* If we found a single quote the match it  */
	if(  Found_Str == ''''  ) {
		Quote_Loop:
			if(  Direction == 1  ) {
				RIGHT;
			} else {
				LEFT;
			}
			if(  Direction == 1  ) {
				if( cmac ) {
					search_fwd( '''', 1 );
				} else {
					if( Search_Fwd('{\''}||''',1) ) {
						if (found_str == '\''') {
							right;
							goto quote_loop;
						}
					}
				}
			} else {
				if( Search_Bwd('''||{@/@/}',1) ) {
					if( !(found_str == '|47|47') ) {
						if( !cmac ) {
							LEFT;
							if( cur_char == '\' )
								goto quote_loop;
							RIGHT;
						}
					}
				}
			}
//			if(  S_Res == 0  ) {
//				Goto Error_Exit;
//			}
			Goto Match_Loop;
	}

											/* If we found a double quote then match it */
	if(  Found_Str == '"'  ) {
		Quote_Loop2:
			if(  Direction == 1  ) {
				RIGHT;
			} else {
				LEFT;
			}
			if(  Direction == 1  ) {
				S_Res = Search_Fwd('[\"]',1);
				if( s_res ) {
					if (found_str == '\') {
						right;
						goto quote_loop2;
					}
				}
			} else {
				S_Res = Search_Bwd('"||{@/@/}',1);
				if( s_res ) {
					if( !(found_str == '|47|47') ) {
						LEFT;
						if( cur_char == '\')
							goto quote_loop2;
						RIGHT;
					}
				}
			}
//		if(  S_Res == 0  ) {
//				Goto Error_Exit;
//			}
			Goto Match_Loop;
	}

											/* If we found a opening comment then match it */
	if(  (Found_Str == '/')  ) {
		if(  (Direction == 1)  ) {
			Right;
			if(  (Cur_Char == '*')  ) {
				S_Res = Search_Fwd('@*/',scope);
				right; right;
			} else if( cur_char == '/' ) {		// Handle possibility of C++ comments
				eol;
			} else {
				left;
			}
			Goto Match_Loop;
		} else {
			if(  c_col > 1  ) {
				Left;
														/* If we found a closing comment then match it */
				if(  (Cur_Char == '*')  ) {
						S_Res = Search_Bwd('/@*',scope);
						left;
				} else {
					right;
				}
			} else if(  (c_line == 1)  ) {
				s_res = 0;
			}
			Goto Match_Loop;
		}
	}



Error_Exit:       /* We go here if no match was found */
	Goto_Mark;
	if( scope > 0 )
			make_message('Match NOT found within ' + str(scope) + ' lines.');
	else
			Make_Message('Match NOT Found.');
	Goto Macro_Exit;

Found_Exit:       /* We go here if a match was found */

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
//    if((f_line > t_line) || ((f_line == t_line) && (f_col >= t_col)) )
//      block_col2 = block_col2 + 1;
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
  stream_block_mode = tstream_block;
	Refresh = OldRefresh;
	Redraw;
	Pop_Undo;
}

macro BUILDCMT {
/* ******************************************************************************
																MULTI-EDIT MACRO

Name:	BUILDCMT

Description:	Builds a C comment block. (called by Language^Template)

							 (C) Copyright 1992 by American Cybernetics, Inc.
****************************************************************************** */

	int  ic,    /*  the indent column  */
					 jx,    /*  general purpose  */
					 jk,    /*  general purpose  */
					 ti     /*  temp insert mode  */
				 ;

	str  tstr   /*  general purpose  */
				 ;

	push_undo;

	ti = insert_mode;
	insert_mode = true;
	mark_pos;
	while(  (get_line == '') & Not(At_EOF)  ) {
		down;
		First_Word;
	}
	if(  At_EOF  ) {
		Goto_Mark;
		Text('  */');
		Left;
		Left;
		Left;
		Goto Exit;
	}
	Set_Indent_Level;
	ic = C_Col;

	if(  (ic == 1)  ) {
		if(  (search_fwd('(',1))  ) {
			mark_pos;
			if(  (search_fwd(')',0))  ) {
				right;
				if(  (cur_char != ';')  ) {
					goto_mark;
					goto build_header;
				}
			}
			goto_mark;
		}
	}

	Goto_Mark;
	Goto_Col( ic );

/* If you don't like the fancy comment construct with the time and date, simply
remove the comments off the following 5 lines, and a very simple comment will
be created.
	Text('  */');
	Left;
	Left;
	Left;
	Goto Exit;
 */
	jx = 0;
	while(  (c_col < 79) & (jx < 50)  ) {
		text('-');
		++jx;
	}
	jk = ic + (jx / 2) - 8;
	if(  jk < (ic + 2)  ) {
		jk = ic + 2;
	}
	goto_col( jk );
	insert_mode = false;
	text( date + ' ' + str_del( time, 6, 3) );
	insert_mode = true;
	eol;

	rm('C_IND');
	Mark_Pos;
	rm('C_IND');

	jk = 0;
	while(  (jk < jx)  ) {
		text('-');
		++jk;
	}
	text('*/');
	Goto_Mark;

	goto exit;

build_header:
	first_word;
	tstr = get_word('(');
	goto_mark;
	Goto_Col( 1 );
	set_indent_level;
	text('/************************************************');
	rm('cr');
	text(' *');
	jk = 24 - ((Length(tstr) + 8) / 2);
	if(  jk < 3  ) {
		jk = 3;
	}
	goto_col(jk);
	text('--- ' + tstr + ' ---');
	cr;
	text(' *');
	cr;
	text(' ************************************************/');
	up;
	eol;
	indent;
Exit:
	insert_mode = ti;
	pop_undo;

}

/* ******************************************************************************
																MULTI-EDIT MACRO

Name:	CSETX

Description:	.C file name extension initialization code.  Sets up the .C
							specific global variable for template expansion.  see macro
 							called "Template" in LANGUAGE.S for details of special characters found in
							the global string "!C.Tmplt0".

							 (C) Copyright 1992 by American Cybernetics, Inc.
****************************************************************************** */
macro CSETX
{
	if ( !Global_Int("@DA_AB_MATCH")  ) {
		key_to_window( <)>, 'c_close_paren' );
		key_to_window( <}>, 'c_close_brace' );
	}
	// Initialize .C template expasion global variable
	if ( ""==Global_Str("!C.Tmplt0") )
	{
		Set_Global_Str("!C.Tmplt0", "ÓC=0ÓM=1"+
		"/"+												// ÓC=0 case sensitive
		"Ä/*˚C^BuildCMT"+          // ÓM=1 minimum expansion of 1 character
		"/*˚C^BuildCMT"+
		"{ƒÆ}"+
		"#define "+
		"#else"+
		"#elif "+
		"#eilf "+
		"#endif"+
		"#error "+
		"#include <ƒ>"+
		'#i"nclude ƒ"'+
		'#i<nclude ƒ>'+
		"#if "+
		"#ifdef "+
		"#idfef "+
		"#ifndef "+
		"#pragma "+
		"#undef "+
		"break;"+
		"case ƒ:"+
		"continue;"+
		"class ƒ Å¸;"+
		"do Å¸ while ( ƒ );"+
		"default:"+
		"else"+
		"else if ( ƒ ) Å¸"+
		"eilse f ( ƒ ) Å¸"+
		"extern ƒ;"+
		"for ( ƒ ) Å¸"+
		"if ( ƒ ) Å¸"+
		"goto ƒ;"+
		"return ƒ;"+
		"switch ( ƒ ) Å®¸≠case :"+
		"struct ƒ Å¸;"+
		"typedef ƒ;"+
		"union ƒ Å¸;"+
		"while ( ƒ ) Å¸"
		);

		switch ( c_style )
		{
			case 3 :
								set_global_str("!C.Tmplt0", global_str("!C.Tmplt0") +
												"ÅÒ{Ò}" );
								break;

			case 2 :
			case 1 :
								set_global_str("!C.Tmplt0", global_str("!C.Tmplt0") +
												"ÅÆÒ{ÒÆ}" );
								break;

			default :
								set_global_str("!C.Tmplt0", global_str("!C.Tmplt0") +
												"ÅÒ{ÒÆ}" );
								break;
		}
	}
}


/*-----------------09-16-91 00:00am-----------------
 * Highlights to matching open paren when a closing
 * paren is entered.
 *--------------------------------------------------*/
macro c_close_paren
{
	push_undo;
	text(')');
	left;
	rm('CMTCH /RC=1/HI=1/LS=20');
	right;
	pop_undo;
}

/*-----------------09-16-91 00:00am-----------------
 * Sets the indenting correctly when a closing brace
 * is entered.
 *--------------------------------------------------*/
macro c_close_brace
{
	int jx = c_col;
	push_undo;
	refresh = false;
	text('}');
	first_word;
	if(jx == c_col) {
		mark_pos;
		rm('CMTCH');
		first_word;
		jx = c_col;
		goto_mark;
		del_char;
		goto_col(jx);
		text('}');
	}
	else {
		goto_col( jx );
		right;
	}
	pop_undo;
}
