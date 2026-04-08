macro_file TEXT;
/****************************MULTI-EDIT MACRO FILE******************************

CHNGCASE					- Changes the case of a line, word, or block
TEXTSORT					- Sorts lines of text
CENTER						- Centers a line
OPENLN						- Insert a blank line while in overwrite mode
REFORMAT					- Reformats a paragraph
JUSTIFY						- Justifies a paragraph
UNJUSTIF					- Unjustifies a paragraph
RULER							- Displays a character ruler at the cursor position
END_OF_PARAGRAPH	- Finds the end of the current paragraph
NEXT_PARAGRAPH		- Finds the beginning of the next paragraph
MARKPOS						- Mark a position using the stack bookmarks
GOTOMARK					- Return to a mark on the stack
SET_MARK					- Mark a position using random marks
GET_MARK					- Return to a random mark

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

macro CHNGCASE TRANS2 {
/********************************MULTI-EDIT MACRO******************************

Name:  CHNGCASE

Description:  Changes the case of a block of text.

Parameters:  /U=1			Uppercase
							 =0			Lowercase
						 /T=0			Line
							 =1			Word
							 =2			Block

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  jx, tc, ti, casetype, blocktype,
			 tb1, tbs, tb2, tbl1, tbl2, tbc1, tbc2,
			 tpb = persistent_blocks;
	str  tstr ;

	push_undo;
	refresh = false;
	ti = insert_mode;
	insert_mode = true;
	casetype = parse_int('/U=', mparm_str);
	blocktype = parse_int('/T=', mparm_str);

	if(  blocktype == 0  ) {
		if(  casetype  ) {
			put_line( caps(get_line));
		} else {
			put_line( lower(get_line));
		}
	}
	if(  blocktype == 1  ) {
		if(  not(at_eol) & (xpos(cur_char,word_delimits,1) != 0)  ) {
			word_right;
		} else {
			if(  c_col > 1  ) {
				left;
				if(  xpos(cur_char,word_delimits,1)  ) {
					right;
				} else {
					right;
					word_left;
				}
			}
		}
		tc = c_col;
		tstr = get_word(word_delimits);
		if(  casetype  ) {
			tstr = caps(tstr);
		} else {
			tstr = lower(tstr);
		}

		goto_col(tc);
		del_chars( svl(tstr) );
		text( tstr );
	}
	if(  (block_stat != 0) & (blocktype == 2)  ) {
		persistent_blocks = TRUE;
		block_end;
		tbl1 = block_line1; tbl2 = block_line2;
		tbc1 = block_col1; tbc2 = block_col2;
		tbs = block_stat;
		mark_pos;
		jx = block_line1;
		while(  jx <= tbl2  ) {
			goto_line(jx);
			if(  tbs == 1  ) {
				if(  casetype  ) {
					put_line( caps(get_line));
				} else {
					put_line( lower(get_line));
				}
			}
			if(  tbs == 2  ) {
				goto_col(tbc1);
				tstr = copy(get_line, tbc1, tbc2 - tbc1 + 1);
				del_chars( svl(tstr) );
				if(  casetype  ) {
					tstr = caps(tstr);
				} else {
					tstr = lower(tstr);
				}
				text( tstr );
			}
			if(  tbs == 3  ) {
				goto_col(1);
				tb1 = 1;
				tstr = get_line;
				tb2 = svl( tstr );
				if(  jx == tbl1  ) {
					 goto_col(tbc1);
					 tb1 = tbc1;
				}
				if(  jx == tbl2  ) {
					tb2 = tbc2;
				}
				del_chars( tb2 - tb1 + 1);
				tstr = copy( tstr, tb1, tb2 - tb1 + 1);
				if(  casetype  ) {
					tstr = caps(tstr);
				} else {
					tstr = lower(tstr);
				}
				text(tstr);
			}
			++jx;
			down;
		}
		if(  tbs == 3  ) {
			goto_line(tbl1);
			goto_col(tbc1);
			str_block_begin;
			goto_line(tbl2);
			goto_col(tbc2 + 1);
			block_end;
		}
		goto_mark;
	}
	insert_mode = ti;
	pop_undo;
	persistent_blocks = tpb;
	redraw;
}

macro TEXTSORT TRANS2 {
/********************************MULTI-EDIT MACRO******************************

Name:	TextSort

Description:	Sorts lines of text.

Parameters:		/C=nn			Starting column of sort field.
							/L=nn			# of characters in sort field.
							/CASE			Ignore case of field.
							/D				Sort in descending order.
							/B				Sort the marked line block.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  tw, old_block_stat,
					 descending,
					 caps_on, sl, el,
					 c,
					 l,
					 tpb = persistent_blocks;
					 ;

		persistent_blocks = TRUE;
		push_undo;
		tw = wrap_stat;
		wrap_stat = false;
		insert_mode = true;
		refresh = false;
		working;
		mark_pos;
		old_block_stat = 0;
		Descending = xpos( '/D', MParm_Str, 1 ) == 0;
		Caps_On = Xpos( '/CASE', MParm_Str, 1 ) != 0;
		c = parse_int( '/C=', MParm_Str );
		l = parse_int( '/L=', MParm_Str );
		if(  c == 0  ) {
			c = 1;
		}
		if(  l == 0  ) {
			l = 2048;
		}

		if(  xpos( '/B', MParm_Str, 1 ) != 0  ) {
			if(  Block_Stat != 1  ) {
				Make_Message( 'No line block marked.');
				goto exit;
			}
			old_block_stat = block_stat;
			sl = block_line1;
			el = block_line2;
			block_off;
		} else {
			eof;
			sl = 1;
			el = c_line;
			tof;
		}
		Make_Message( 'Sorting...' );
		qsort_lines( sl, el, descending, c, l, caps_on );
		Make_Message( 'Text sorted.');


exit:
		if(  old_block_stat > 0  ) {
			goto_line(sl);
			block_begin;
			goto_line(el);
			block_end;
		}
		wrap_stat = tw;
		pop_undo;
		refresh = true;
		goto_mark;
		persistent_blocks = tpb;

}

macro CENTER TRANS2 {
/********************************************************************************
																MULTI-EDIT MACRO

Name: CENTER

Description: Centers the line the cursor is on between column 1 and the right
							margin.


							 (C) Copyright 1991 by American Cybernetics, Inc.
********************************************************************************/

	int jx,oim=Insert_Mode;
	str tstr;
	refresh = false;
	Goto_Col(1);
	Push_Undo;
	Insert_Mode = True;
/* Delete leading spaces */
	while(  NOT(At_EOL) & (XPOS(Cur_Char,'|255|9|32',1) != 0)  ) {
		Del_Char;
	}
/* Store line into variable */
	Tstr = Shorten_Str(Get_Line);
	jx = Svl(Tstr);
	if(  jx > 0  ) {
		jx = (Right_Margin / 2) - (jx / 2);
/* Blank out existing line */
		Put_line('');
		Goto_Col(jx);
/* Put line at new position.  Works with tabs as spaces, or tabs. */
		Text(TStr);
	}
	redraw;
	Pop_Undo;
	Insert_mode=oim;
}


macro OPENLN TRANS2 {
/********************************************************************************
																MULTI-EDIT MACRO

Name: openln

Description: Opens a line below the current line, without moving the cursor.

							 (C) Copyright 1991 by American Cybernetics, Inc.
********************************************************************************/

	int  temp_ins ;
	push_undo;
	temp_ins = insert_mode;
	insert_mode = true;
	Mark_Pos;
	CR;
	Goto_Mark;
	Make_Message('Line opened.');
	insert_mode = temp_ins;
	pop_undo;
}

macro REFORMAT TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	REFORMAT

Description: Reformats the current paragraph from the current cursor position
						 down.

Parameters:
									/RC=n		1 = Restore Cursor position when done

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  if (read_only) {
    RM("MESSAGEBOX /B=2/T=ERROR/M=Can't reformat a paragraph in a read-only window!");
  } else {
    Push_Undo;
    Word_Wrap_Line( TRUE, Parse_Int('/RC=',MParm_Str) != 0);
    Pop_Undo;
    Make_Message('Paragraph reformatted.');
  }
}

macro JUSTIFY TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	JUSTIFY

Description: Justifies the current paragraph from the current cursor position
						 down.

Parameters:
									/RC=n		1 = Restore Cursor position when done

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  jx,jy,rr,jk, ti, tw, OldRefresh = Refresh,
					rc = Parse_Int('/RC=',Mparm_Str) ;

	push_undo;
	tw = wrap_stat;
	wrap_stat = false;
	ti = insert_mode;
	insert_mode = true;
	refresh = false;
	if(  Right_Margin > 132  ) {
		Make_Message('Right Margin too big.');
		Goto Exit2;
	}
	Make_Message('Justifying paragraph...');
	if(  (rc)  ) {
		Mark_Pos;
	}
	Working;
MAIN_LOOP:
	down;
	goto_col(1);
	if(  AT_EOL  ) {
		goto EXIT;
	}
	if(  Xpos(page_str,get_line,1)  ) {
		goto exit;
	}
	up;
M2:
	first_word;
/* remove trailing spaces */
	put_line( Shorten_Str(Get_Line) );
	jx = 0;

/* Count existing spaces */
SPACE_LOOP:
	if(  cur_char == ' '  ) {
		jx = jx + 1;
	}
	right;
	if(  NOT( AT_EOL )  ) {
		goto space_loop;
	}
	eol;
	left;
	jy = Right_Margin - c_col;
	if(  (jy <= 0) | (jX <= 0)  ) {
		goto NEXT_LINE;
	}
	rr = jx / jy;
	first_word;
	jx = 0;
	jk = 0;
/* Add in extra spaces until desired line length is reached */
	while(  NOT( AT_EOL )  ) {
		if(  cur_char == ' '  ) {
			jx = jx + 1;
			if(  jx >= rr  ) {
				if(  jk < jy  ) {
					jk = jk + 1;
					text(' ');
					right;
				}
				jx = 0;
			}
		}
		right;
	}

/* Repeat until at end of paragraph */
	if(  c_col > Right_Margin  ) {
		goto NEXT_LINE;
	}
	GOTO M2;
NEXT_LINE:
	down;
	eol;
	if(  c_col == 1  ) {
		goto EXIT;
	}
	if(  hard_cr != ''  ) {
		if(  xpos(hard_cr, get_line, 1)  ) {
			goto EXIT;
		}
	}

	GOTO MAIN_LOOP;

EXIT:
/* Move back to origninal position or to next paragraph */
	if(  (rc)  ) {
		Goto_Mark;
	} else {
		while ((get_line == "") && (!at_eof)) {
			Down;
		}
		first_word;
	}
	Make_Message('Paragraph justified.');
EXIT2:
	refresh = OldRefresh;
	wrap_stat = tw;
	pop_undo;
	redraw;
	insert_mode = ti;
}

macro UNJUSTIF TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	UNJUSTIF

Description: Unjustifies the current paragraph from the current cursor position
						 down.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  tw, OldRefresh = refresh;
	push_undo;
	tw = wrap_stat;
	wrap_stat = false;

	mark_pos;
	refresh = false;

	Make_Message('Unjustifying paragraph...');
	Working;
main_loop:
	first_word;
/* Check for end of paragraph */
	if(  AT_EOL  ) {
		goto EXIT;
	}
	if(  hard_cr != ''  ) {
		if(  xpos(hard_cr, get_line, 1)  ) {
			goto EXIT;
		}
	}
	while(  NOT( AT_EOL )  ) {
/* Skip over end of sentences, etc. */
		if(  pos(cur_char,'.!?:')  ) {
			right;
			right;
		}
/* Otherwise, remove occurances of more than one space */
		if(  cur_char == ' '  ) {
			right;
			while(  cur_char == ' '  ) {
				del_char;
			}
		}
		right;
	}
	down;
	goto Main_Loop;
EXIT:
	Make_Message('Paragraph Unjustified...');
	wrap_stat = tw;
	pop_undo;
	refresh = OldRefresh;
	goto_mark;
	redraw;

}

macro RULER TRANS2 {
/********************************MULTI-EDIT MACRO******************************

Name: RULER

Description:  Puts up a character ruler at the cursor position.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str  tstr, tstr2 ;
	int  jx, jy, jl, jk, old_col, t_line, offset, omm, ptr_removed ;
	refresh = TRUE;
	call calc_offset;
	redraw;
	old_col = c_col;
	tstr = '';
	tstr2 = '';
	jx = 0;
	jy = 1;
	jl = 0;
	while(  jx < 254  ) {
		tstr = tstr + str(jy);
		++jx;
		++jy;
		if(  jy == 10  ) {
			++jx;
			jy = 1;
			tstr = tstr + '0';
			++jl;
			tstr2 = tstr2 + copy('様様様様様', 1,10 - length(str(jl))) + str(jl);
		}
	}
	omm = mouse_mode;
	ptr_removed = FALSE;
	make_message('Use |27|26 arrow keys, <HOME> and <END> to move.  Exit<ESC>.');
	set_virtual_display;
loop:
	call put_ruler;
	update_virtual_display;
	while(  NOT(check_key)  ) {
		Mou_Check_Status;
		if(  ((Mou_Last_Status & 1) == 0)  ) {
			mouse_mode = omm;
			if(  ptr_removed  ) {
				--ptr_removed;
				mou_set_pos( wherex, wherey );
				mou_draw_ptr;
			}
		}
	}
	if(  key1 == 27  ) {
		goto exit;
	}
	if(  key1 == 0  ) {
		if(  (key2 == 245) | (key2 == 251)  ) {
			goto exit;
		} else if(  (key2 == 250)  ) {
			if(  (mou_last_x > win_x1) & (mou_last_x < win_x2) &
						(mou_last_y == wherey)  ) {
				RM('MouseInWindow');
				if(  return_int  ) {
					mouse_mode = TRUE;
					mou_reset;
					mou_remove_ptr;
					++ptr_removed;
				}
			}
		} else if(  (c_col < 2048) & ((key2 == 77) | (key2 == 243))  ) {
			right;
		} else if(  (c_col > 1) & ((key2 == 75) | (key2 == 242))  ) {
			left;
		} else if(  (key2 == 72) | (key2 == 141)   ) {
			up;
			call calc_offset;
			redraw;
		} else if(  (key2 == 80) | (key2 == 145)  ) {
			down;
			call calc_offset;
			redraw;
		} else if(  (key2 == 116) & (old_col < 2048)  ) {
			if(  (c_col < 2048)  ) {
				right;
			}
			++old_col;
			redraw;
		} else if(  (key2 == 115) & (old_col > 1)  ) {
			if(  c_col > 1  ) {
				left;
			}
			--old_col;
			redraw;
		} else if(  key2 == 71  ) {
			goto_col(old_col);
		} else if(  key2 == 79  ) {
			eol;
		}
	}
	goto loop;

calc_offset:
	if(  c_row < 3  ) {
		offset = 1;
	} else {
		offset = -2;
	}
	ret;

put_ruler:
	jy = c_col - (wherex - win_x1);
	jk = 1;
	if(  old_col <= jy  ) {
		jk = jy - old_col + 2;
	}
	jx = old_col - jy;
	if(  jx < 1  ) {
		jx = 1;
	}

	jl = win_x2 - (win_x1 + jx);

	write( copy(tstr2, jk, jl), win_x1 + jx, wherey + offset, 0, h_color );
	write( copy(tstr, jk, jl), win_x1 + jx, wherey + offset + 1, 0, h_color );
	ret;

no_room:
	Make_Message('No room in window for ruler.');
	goto exit2;
exit:
	make_message('Ruler done.');
exit2:
	reset_virtual_display;
	while(  ptr_removed > 0  ) {
		--ptr_removed;
		mou_set_pos( wherex, wherey );
		mou_draw_ptr;
	}
	mouse_mode = omm;
	redraw;
}

macro END_OF_PARAGRAPH trans2 {
/********************************MULTI-EDIT MACRO******************************

Name: END_OF_PARAGRAPH

Description:  Finds the end of the current paragraph.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	Refresh = FALSE;
LOOP:
	if(  (copy(get_line, 1, length(page_str)) == page_str)  ) {
		goto EXIT;
	}
	EOL;
	if(  (c_col == 1) | (At_EOF)  ) {
		goto EXIT;
	}
	LEFT;
	if(  cur_char == Hard_CR  ) {
		goto exit;
	}
	DOWN;
	GOTO LOOP;
EXIT:
}

macro NEXT_PARAGRAPH trans2 {
/********************************MULTI-EDIT MACRO******************************

Name: NEXT_PARAGRAPH

Description:  Finds the beginning of the next paragraph.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	RM( 'End_Of_Paragraph' );
	Refresh = FALSE;
LOOP:
	DOWN;
	First_Word;
	if(  (cur_char == Hard_Cr) | (At_EOL) |
			(copy(get_line, 1, length(page_str)) == page_str)  ) {
		if(  Not(At_EOF)  ) {
			Goto LOOP;
		}
	}
}


macro MARKPOS trans2 {
/********************************MULTI-EDIT MACRO******************************

Name: MARKPOS

Description:  Just acts as an interface to the macro function MARK_POS.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	mark_pos;
	make_message('Position marked.');
}

macro GOTOMARK trans2 {
/********************************MULTI-EDIT MACRO******************************

Name: GOTOMARK

Description:  Just acts as an interface to the macro function GOTO_MARK.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	if( mark_stack_count == 0 ) {
		make_message('No markers on stack.');
	} else {
		goto_mark;
		make_message('Position retrieved.');
	}
}

macro SET_MARK trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:		SET_MARK

Description:	Uses the new random access marker stack to set 10 markers.
				If SET_MARK is executed without parameters, the user is prompted
				to press a number key (0-9, 0=10) to specify the marker #.  If a
				value of 1-10 is passed as a parameter, then the specified mark
				is set without prompting the user.

Parameters:		MParm_Str can contain 1-10 to denote going directly to the mark.
							If the entire MParm_Str does not contain numeric characters, this
							option will not work, and the menu will be invoked.  Under these
							circumstances, the following parameters are expected:
							/X=		The X coordinate for the menu
							/X=		The X coordinate for the menu
							/BC=	The amount of boxes which need to be killed upon exit

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  jx, bc, jk, line, position, row, offset;
	int	 menu = menu_create;

		/* If a numeric parameter was passed then use it, and bypass
		 the prompting */
	if(  Val(jx, MParm_Str) == 0  ) {
		if(  (jx > 0) & (jx < 11)  ) {
			goto do_set;
		}
	}
	for( jk = 1; jk <= 9; ++jk ) {
		get_mark_record( jk, 2, line, position, row, offset );
		if ( line <= 0 ) {
			menu_set_item( menu, jk, ' ' + str( jk ) + ' ', '', '', 0, 0, 0 );
		}
		else {
			menu_set_item( menu, jk, '|175' + str( jk ) + ' ', '', '', 0, 0, 0 );
		}
	}
	get_mark_record( 10, 2, line, position, row, offset );
	if ( line <= 0 ) {
		menu_set_item( menu, 10, ' 0 ', '', '', 0, 0, 0 );
	}
	else {
		menu_set_item( menu, 10, "\xAF\x30 ", '', '', 0, 0, 0 );
	}
//	menu_set_item( menu, 10, ' 0 ', '', '', (line <= 0), 0, 0 );
	return_int = menu;
	RM('USERIN^TOPMENU /HN=1/#=10/H=PMRANDOM/L=Select marker # to set/X=' +
							parse_str('/X=', mparm_str) +
							'/Y=' + parse_str('/Y=', mparm_str) );

/*	RM('USERIN^XMENU /B=1/L=Select marker # to set/X=' + parse_str('/X=', mparm_str) +
							'/Y=' + parse_str('/Y=', mparm_str) +
																	 '/M=1 (PMRANDOM)2 ()3 ()4 ()5 ()6 ()7 ()8 ()9 ()0 ()');
*/
	if(  return_int <= 0  ) {
		goto exit;
	}
	jx = return_int;
	return_int = 100;
do_set:
	bc = parse_int('/BC=', mparm_str);
	while(  box_count > bc  ) {
		kill_box;
	}
	set_mark( jx );
	make_message('Marker #' + str(jx) + ' set.' );
exit:
	menu_delete( menu );
}

macro GET_MARK no_break trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:		GET_MARK

Description:	Uses the new random access marker stack to retrieve 10 markers.
						If GET_MARK is executed without parameters, the user is prompted
						to press a number key (0-9, 0=10) to specify the marker #.  If a
						value of 1-10 is passed as a parameter, then the specified mark
						is retrieved without prompting the user.

Parameters:		MParm_Str can contain 1-10 to denote setting that particular mark.
							If the entire MParm_Str does not contain numeric characters, this
							option will not work, and the menu will be invoked.  Under these
							circumstances, the following parameters are expected:
							/X=		The X coordinate for the menu
							/X=		The X coordinate for the menu
							/BC=	The amount of boxes which need to be killed upon exit

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  jx, bc,
			 menu = menu_create,
			 line, offset, row, position,
			 jk
			 ;

		/* If a numeric parameter was passed then use it, and bypass
		 the prompting */
	if(  Val(jx, MParm_Str) == 0  ) {
		if(  (jx > 0) & (jx < 11)  ) {
			goto do_get;
		}
	}
	for( jk = 1; jk <= 9; ++jk ) {
		get_mark_record( jk, 2, line, position, row, offset );
		if ( line <= 0 ) {
			menu_set_item( menu, jk, ' ' + str( jk ) + ' ', '', '', 1, 0, 0 );
		}
		else {
			menu_set_item( menu, jk, '|175' + str( jk ) + ' ', '', '', 0, 0, 0 );
		}
	}
	get_mark_record( 10, 2, line, position, row, offset );
	if ( line <= 0 ) {
		menu_set_item( menu, 10, ' 0 ', '', '', 1, 0, 0 );
	}
	else {
		menu_set_item( menu, 10, "\xAF\x30 ", '', '', 0, 0, 0 );
	}
//	menu_set_item( menu, 10, ' 0 ', '', '', (line <= 0), 0, 0 );
	return_int = menu;
	RM('USERIN^TOPMENU /HN=1/#=10/H=PMRANDOM/L=Select marker # to retrieve/X=' +
							parse_str('/X=', mparm_str) +
							'/Y=' + parse_str('/Y=', mparm_str) );
	if(  return_int <= 0  ) {
		goto exit;
	}
	jx = return_int;
	return_int = 100;
do_get:
	bc = parse_int('/BC=', mparm_str);
	while(  box_count > bc  ) {
		kill_box;
	}
	get_mark( jx );
	make_message('Marker #' + str(jx) + ' retrieved.' );
exit:
	menu_delete( menu );
}