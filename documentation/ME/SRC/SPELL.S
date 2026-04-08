macro_file SPELL;
/*******************************************************************************
														MULTI_EDIT MACRO FILE

Name: SPELL

Description:	This is the spell checker macros.

SPELL						- Brings up the main spell checker menu
_MN_SPELL				- Sets up menu items for SPELL.
SPELLCHK				- The spell checker main interface
SPLSETF					- Retrieves spell checker setup data from file
SPELLCHECKSOURCE- Spell Checks only words between single, or double quotes.
SPLSETUP				- The spell checker setup interface

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

macro SPELL {
/*******************************************************************************
														MULTI_EDIT MACRO FILE

Name: SPELL

Description:	This brings up spell checker main menu.

Parameters:
							/X=		X coordinate for menu box
							/Y=		Y coordinate for menu box
							/BC=	Amount of boxes the menu system currently has

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	rm('MENU /MN=SPELL' + mparm_str );
}

macro _MN_SPELL  {
/*******************************************************************************
														MULTI_EDIT MACRO FILE

Name: _MN_SPELL

Description:	Sets up menu items for SPELL.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	menu_set_item( return_int, 1, "Check from cursor position", "", "/S=2/H=SPELL^*/ CMD=SPELLCHK /T=0TYPE=1CS=0", 0, 0, 0);
	menu_set_item( return_int, 2, "Look up word under cursor", "", "/S=2/H=SPELL^*/ CMD=SPELLCHK /T=1TYPE=1CS=0", 0, 0, 0);
	menu_set_item( return_int, 3, "check marked Block", "", "/S=2/H=SPELL^*/ CMD=SPELLCHK /T=2TYPE=1CS=0", 0, 0, 0);
	menu_set_item( return_int, 4, "check current Page", "", "/S=2/H=SPELL^*/ CMD=SPELLCHK /T=3TYPE=1CS=0", 0, 0, 0);
	menu_set_item( return_int, 5, "check text in Quotes", "", "/S=2/H=SPELL^*/ CMD=SpellCheckSourceTYPE=1CS=0", 0, 0, 0);
	menu_set_item( return_int, 6, "", "", "/S=2/H=/ TYPE=6CS=0", 1, 0, 0);
	menu_set_item( return_int, 7, "Setup spell check...", "", "/S=2/H=SPELL^*/ CMD=SPLSETUPTYPE=1CS=0", 0, 0, 0);
	return_str = "/#=7/HZ=0/ED=0/H=/TRANS=0/T=SPELL/M=";
}

macro SPELLCHK {
/*******************************************************************************
														MULTI_EDIT MACRO FILE

Name:  SPELLCHK

Description: Performs an actual spellcheck.

Parameters:		/T=nn		Spell check type.
											0 = Check document from cursor position
											1 = Lookup word under cursor
											2 = Check block
											3 = Check page
							/NCD=1  Do NOT CLOSE DICTIONARIES when upon completion.
              /CD=1   Just close the dictionaries and exit.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str  orig_word[80], tstr[80], tstr2[80], lstr, prev_word[80],
					 forced_match[2], lchar[1], word_chars[128] ;
	str  ignore_word[40], ignore_char[40] ;
	int  tbc, need_l,need_w,x,y,ee, jy, jz, jx, tc,tc2, box_offset, menu_start,
			aux_window, aux1_window, aux2_window, work_window, cur_dic, list_window,
			ignore_window, t_word_wrap ,T_Insert_Mode;
	int  min_word_size, op_type, word_count, word_result, screen_stat, tr ;
	int  common_result ;
	int tpb = persistent_blocks;
	char  tchar, pchar ;
	persistent_blocks = TRUE;
	t_word_wrap = wrap_stat;
	T_Insert_Mode = Insert_Mode;
	Insert_Mode = true;
	work_window = window_id;
	aux_window = 0;
	aux1_window = GLOBAL_INT('SPELL_AUX1_WINDOW');
	aux2_window = GLOBAL_INT('SPELL_AUX2_WINDOW');
	ignore_window = GLOBAL_INT('SPELL_IGNORE_WINDOW');
	list_window = GLOBAL_INT('SPELL_LIST_WINDOW');
	Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') + 1);

	if(  parse_int('/CD=', mparm_str) == 1  ) {
		goto close_dictionaries;
	}

	if(  global_str('SPELL_MAIN_DIC') == ''  ) {
		RM('splsetf');
	}

	op_type = parse_int('/T=', mparm_str);
	if(  op_type == 1  ) {
		mark_pos;
	}
	if(  op_type == 2  ) {
		if(  block_stat == 0  ) {
			make_message('No block marked.');
			goto exit;
		}
		block_end;
		goto_line( block_line1 );
		goto_col( block_col1 );
		redraw;
	}
	if(  op_type == 3  ) {
		reg_exp_stat = false;
		if(  search_bwd( page_str, 0 )  == 0  ) {
			tof;
		}
		reg_exp_stat = true;
		pchar = copy(page_str, 1, 1 );
	}
	tstr = global_str('SPELL_PARMS');
	min_word_size = parse_int('/WS=',tstr) - 1;
	screen_stat = parse_int('/SCRN=',tstr);
	wrap_stat = (parse_int('/WW=',tstr) & wrap_stat);

	tbc = box_count;


	word_chars = global_str('SPELL_CHARS');
	ignore_word = global_str('SPELL_IGNORE_W');
	ignore_char = global_str('SPELL_IGNORE_C');

	orig_word = global_str('SPELL_COMMON_DIC');
	if(  get_path(orig_word) == ''  ) {
		orig_word = me_path + orig_word;
	}

	if(  spell_open_dic(orig_word, 4, 1, 0 ) == false  ) {
	}

	orig_word = global_str('SPELL_MAIN_DIC');
	if(  get_path(orig_word) == ''  ) {
		orig_word = me_path + orig_word;
	}


	if(  spell_open_dic(orig_word, 1, 1, 2048 ) == false  ) {
		make_message('Cannot open main dictionary "' + global_str('SPELL_MAIN_DIC') + '".');
		word_result = -1;
		goto exit;
	}

	orig_word = global_str('SPELL_AUX1_DIC');
	if(  get_path(orig_word) == ''  ) {
		orig_word = me_path + orig_word;
	}
	if(  spell_open_dic( orig_word, 2, 0, 1000 ) == false  ) {
	}

	orig_word = global_str('SPELL_AUX2_DIC');
	if(  get_path(orig_word) == ''  ) {
		orig_word = me_path + orig_word;
	}
	if(  spell_open_dic( orig_word, 3, 0, 1000 ) == false  ) {
	}

	refresh = false;
		/* go to start of first word */

	while(  (xpos(cur_char,'|255 |9',1) != 0) & (c_col > 1)  ) {
		left;
	}

	while(  (xpos(cur_char, '|255() ''"|9[]{}+\-||=*&^%$#@!~`><,.:;?/', 1) == 0)  ) {
		if(  (c_col == 1)  ) {
			goto loop_start;
		}
		left;
	}
	right;


loop_start:
	tstr = '';
	call spell_message;
	word_count = 0;
loop:
	if(  op_type > 0  ) {
		if(  op_type == 1  ) {
			if(  word_count > 0  ) {
				goto exit;
			}
		} else {
				/* restrict to block */
			if(  op_type == 2  ) {
				if(  c_line >= block_line2  ) {
					if(  c_line > block_line2  ) {
						goto check_complete;
					}
					if(  block_stat == 3  ) {
						if(  c_col > block_col2  ) {
							goto check_complete;
						}
					}
				}
				if(  block_stat == 2  ) {
          if ( c_col > block_col2  ) {
						down;
            if ( c_line > block_line2 )
            {
              goto check_complete;
            }
						goto_col( block_col1 );
            //
            // Search back for word crossing block column
            // This is a BACKWARD_TILL_NOT
            //
            while ((C_Col > 1) && (xpos (cur_char, word_chars, 1)))
                left;
            if ((!xpos (cur_char, word_chars, 1)) && (C_Col != block_col1))
                right;
					}
          else if ( c_col < block_col1 )
          {
            goto_col ( block_col1);
            //
            // Search back for word crossing block column
            // This is a BACKWARD_TILL_NOT
            //
            while ((C_Col > 1) && (xpos (cur_char, word_chars, 1)))
                left;
            if ((!xpos (cur_char, word_chars, 1)) && (C_Col != block_col1))
                right;
          }
				}
			}
		}
	}
	prev_word = tstr;


	if(  check_key  ) {
		if(  key1 == 27  ) {
			goto abort_check;
		}
	}

	tc = c_col;
	tchar = lower(cur_char);
	jx = ascii(tchar);
	if(  (jx > 96) & (jx < 169)  ) {
		++word_count;
		refresh = screen_stat;
		orig_word = get_word_in( word_chars );
		right;
	try_again:

			/* Ignore any word with an underscore in it. */
		jz = xpos('_', orig_word, 2);
		if(  jz != 0  ) {
			tstr = orig_word;
			goto do_loop2;
		}

			/* deal with apostrophes */
		jz = xpos('''', orig_word, 2);
		if(  jz != 0  ) {
			if(  (svl(orig_word) == (jz + 1))  ) {
				if(  lower(str_char(orig_word, jz + 1)) == 's'  ) {
					orig_word = str_del( orig_word, jz, 2 );
				}
			} else {
				if(  svl( orig_word ) == jz  ) {
					orig_word = str_del( orig_word, jz, 1 );
				}
			}
		}
		tc2 = c_col;
		tstr = lower(orig_word);
		word_result = true;
		jx = svl(tstr);
		if(  jx > min_word_size  ) {
				/* If the wordlength is less than 9 then check the common words
					dictionary */
			if(  jx < 9  ) {
				common_result = spell_lookup_word( tstr,4 );
			} else {
				common_result = 0;
			}
			if(  common_result == 0  ) {
				if(  spell_lookup_word( tstr, 1 ) == 0  ) {
					if(  spell_lookup_word( tstr, 2 ) == 0  ) {
						if(  spell_lookup_word( tstr, 3 ) == 0  ) {
							if(  switch_win_id( ignore_window )  ) {
								tof;
								if(  search_fwd('%' + tstr + '@||',0)  ) {
									goto_col( svl(tstr) + 2);
									return_str = get_word(' ');
									switch_win_id( work_window );
									if(  return_str != ''  ) {
										goto_col(tc);
										call replace_word;
										goto do_loop;
									}
									goto do_loop4;
								}
								switch_win_id( work_window );
							}
							refresh = true;
							goto_col(c_col);
							word_result = false;
							jx = 0;
							goto_col(tc);
						/* 	goto_v_col( v_col );  */
							box_offset = 0;
							menu_start = 1;
							draw_attr(wherex, wherey, h_color | $80, svl(tstr));
							need_l = 11;
							need_w = svl(tstr) + 17;
							if(  need_w < 25  ) {
								need_w = 25;
							}
							call calc_xy;

							refresh = false;
					menu_again:
							set_global_str('SPELL1', '/H=SPELL^MSPLDUNKWN/N=Ignore');
							set_global_str('SPELL2', '/N=Edit word');
							set_global_str('SPELL3', '/N=Suggest spellings');
							set_global_str('SPELL4', '/N=Add to aux. dictionary 1');
							set_global_str('SPELL5', '/N=add to aux. dictionary 2');
							set_global_str('SPELL6', '/N=iGnore for rest of check');
							set_global_str('SPELL7', '/N=Quit spell check(<ESC>)');
							RM('USERIN^SUBMENU /GCLR=1/G=SPELL/#=7/X=' + str(x + box_offset) +
													'/Y=' + str(y + box_offset) + '/S=' + str(menu_start) +
													'/B=' + str( box_offset ) +
													'/L="' + tstr + '" NOT FOUND');
							menu_start = return_int;
							box_offset = 1;
							if(  (return_int < 1) | (return_int == 7)  ) {
								key1 = 27;
								while(  box_count > tbc  ) {
									kill_box;
								}
								goto abort_check;
							}
							if(  return_int == 1  ) {
								goto_col(tc2);
								kill_box;
								goto do_loop;
							}
							if(  return_int == 2  ) { /* edit the word */
								make_message('Press <ENTER> to replace word with edited word, <ESC> to abort edit.');
								Create_Global_Str('!S_ISTR_1',orig_word);
								Create_Global_Str('!S_IPARM_1','/L=1/W=40/H=SPELL^MSPLDUNKWN');
								RM( 'USERIN^DATA_IN /A=2/PRE=!S_/#=1/T=EDIT WORD/X=' + str(x + 1) + '/Y=' + str(y + 6));
								Return_Str = Global_Str('!S_ISTR_1');
								Set_Global_Str('!S_ISTR_1','');
								make_message('');
								if(  return_int < 1  ) {
									goto menu_again;
								}
								call check_replace;
								if(  tr < 1  ) {
									goto menu_again;
								}
								kill_box;
								word_result = 2;
								call replace_word2;
								if(  tr == 2  ) {
									tstr = lower(orig_word) + '||' + lower(return_str);
									call add_word_to_ignore;
								}
								goto do_loop;
							}
							if(  return_int == 3  ) {
								goto possible_spellings;
							}
							if(  return_int == 4  ) {
								goto_col(tc2);
								kill_box;
								aux_window = aux1_window;
								cur_dic = 2;
								lstr = global_str('SPELL_AUX1_DIC');
								call add_word_to_aux;
								aux1_window = aux_window;
								SET_GLOBAL_INT('SPELL_AUX1_WINDOW', aux1_window);
								goto do_loop;
							}
							if(  return_int == 5  ) {
								goto_col(tc2);
								kill_box;
								aux_window = aux2_window;
								lstr = global_str('SPELL_AUX2_DIC');
								cur_dic = 3;
								call add_word_to_aux;
								aux2_window = aux_window;
								SET_GLOBAL_INT('SPELL_AUX2_WINDOW', aux2_window);
								goto do_loop;
							}
							if(  return_int == 6  ) {
								goto_col(tc2);
								kill_box;
								tstr = tstr + '||';
								call add_word_to_ignore;
								goto do_loop;
							}
				do_loop:
							refresh = true;
							redraw;
							call spell_message;
							refresh = false;
				do_loop4:
						}
					}
				}
			}
		}
 no_check:
		refresh = false;

			/* process double word */
		if(  prev_word == tstr  ) {
			if( svl( tstr ) != 0 ) {
				refresh = true;
				goto_col(c_col);
				goto_col(tc);
				draw_attr(wherex, wherey, h_color | $80, svl(tstr));
				need_l = 7;
				need_w = svl(tstr) + 18;
				if(  need_w < 25  ) {
					need_w = 25;
				}
				call calc_xy;
				RM('USERIN^XMENU /T=1/B=1/X=' + str(x) + '/Y=' + str(y) +
						'/L="' + tstr + '" - DOUBLE WORDS"/M=Ignore(SPELL^DBLWDS)Delete()Quit spell check((<ESC>)()');
				if(  (return_int < 1) | (return_int == 5)  ) {
					key1 = 27;
					goto abort_check;
				}
				if(  (return_int == 2)  ) {
					if(  c_col > 1  ) {
						left;
						if(  cur_char == ' '  ) {
							del_char;
						} else {
							right;
						}
					}
					del_chars( svl(tstr) );
				} else {
					goto_col(tc2);
				}
				refresh = true;
				redraw;
				refresh = false;
				call spell_message;
			}
		}
	} else {
		if(  op_type == 3  ) {
			if(  tchar == pchar  ) {
				if(  word_count > 0  ) {
					if(  copy(get_line, c_col, length(page_str)) == page_str  ) {
						goto check_complete;
					}
				}
			}
		}

		tstr = tchar;
		right;
		if(  xpos(tchar, ignore_word, 1)  ) {
			tstr = lower(get_word_in(word_chars ));
			tstr = '';
		} else {
			if(  xpos(tchar, ignore_char, 1)  ) {
				right;
			}
		}
	}

do_loop2:
	forward_till_not(' |9|255');
eolagain:
	if(  at_eol  ) {
		refresh = screen_stat;
		down;
		first_word;
		if(  at_eof  ) {
			goto check_complete;
		}
		if(  (at_eol)  ) {
			tstr = '';
			goto eolagain;
		}
		if(  screen_stat == 0  ) {
			put_line_num(c_line);
		}
		refresh = false;
	}
	goto loop;


spell_message:
	make_message('Checking...  Press <ESC> to abort.');
	ret;

calc_xy:
	x = (wherex + (svl(tstr) /2)) - (need_w / 2);
	if(  (x + need_w + 2) > screen_width  ) {
		x = screen_width - ( need_w + 2 );
	}
	if(  x < 1  ) {
		x = 1;
	}
	y = wherey - need_l;
	if(  y < 3  ) {
		y = wherey + 2;
		if(  y > (screen_length - need_l)  ) {
			y = 3;
		}
	}
	ret;

add_word_to_ignore:
		aux_window = ignore_window;
		lstr = '';
		cur_dic = 100;
		call add_word_to_aux;
		ignore_window = aux_window;
		SET_GLOBAL_INT('SPELL_IGNORE_WINDOW', ignore_window);
		ret;

possible_spellings:
	make_message('Scanning...');
	lstr = '';
	jx = 0;
	refresh = false;
	if(  not( switch_win_id( list_window ) )  ) {
		switch_window( window_count );
		create_window;
		list_window = window_id;
		SET_GLOBAL_INT('SPELL_LIST_WINDOW',list_window);
	} else {
		erase_window;
	}

	jz = 1;
	jy = 1;
	call lookup_loop;

	jz = 0;
	jy = 2;
	call lookup_loop;


	tchar = copy(tstr,1,1);
	lstr = global_str('SPELL_PHONIC_CHARS');
	jz = xpos(tchar + '=', lstr,1 );
	if(  jz != 0  ) {
		set_global_str('@@@TSTR@@@', tstr );
		jy = xpos(',',lstr,jz);
		if(  jy == 0  ) {
			jy = svl(lstr);
		}
		forced_match = copy(lstr, jz + 2, jy - jz - 2);
		tstr = forced_match + str_del(tstr,1,1);
		jz = 1;
		jy = 1;
		call lookup_loop2;
		tstr = global_str('@@@TSTR@@@' );
		set_global_str('@@@TSTR@@@', '' );
	}

	eof;
	qsort_lines( 1, c_line, 0, 1, 255, 0 );
	tof;
	col_block_begin;
	eof;
	goto_col(1);
  delete_block;
	tof;

	if(  jx == 0  ) {
		make_message('No choices found.');
		switch_win_id( work_window );
		goto menu_again;
	}

	tof;
	make_message('Select word and press <ENTER> to replace, or <ESC> to return to menu.');
	return_str = '';
	Set_Global_Str('SEIPARM_1', '/L=1/T=Select word for "' + Tstr + '":/TP=15/DC=1/C=1/WIN=' + STR(cur_window));
	RM('USERIN^DATA_IN /PRE=SE/A=2/#=1/X=' + str(x + 1) + '/Y=' + str(y + 1) +
	'/H=SPELL^MSPLDUNKWN%Suggest/T=POSSIBLE SPELLINGS');

	Return_Str = Get_Line;

	switch_win_id( work_window );
	if(  return_int == 2  ) {
		goto menu_again;
	}
	if(  return_int == 1  ) {
		call check_replace;
		if(  tr < 1  ) {
			goto menu_again;
		}
		kill_box;
		call replace_word;
		word_result = 2;
		if(  tr == 2  ) {
			tstr = lower(orig_word) + '||' + lower(return_str);
			call add_word_to_ignore;
		}
		goto do_loop;
	}
	goto menu_again;
/*
	kill_box;
	goto do_loop;
 */

	/* jz = 1, start at first char of word */
	/* jz = 0, start at top of dictionary */
	/* tstr = word to find */
	/* jy = dictionary to search */
lookup_loop:
	if(  jz  ) {
		lchar = copy(tstr,1,1);
	} else {
		lchar = '';
	}
	if(  spell_lookup_word( lchar, jy )  ) {
		loop2:
			tstr2 = spell_next_match( tstr, jy );
			if(  svl(tstr2) != 0  ) {
				tstr2 = char( return_int ) + tstr2;
				tof;
				if( !search_fwd( '%' + tstr2 + '$', 0 ) ) {
					eof;
					if(c_col > 1)
							down;
					put_line( tstr2 );
				}
				++jx;
				goto loop2;
			}
	}
	RET;

	/* jz = 1, start at first char of word */
	/* jz = 0, start at top of dictionary */
	/* tstr = word to find */
	/* jy = dictionary to search */
lookup_loop2:
	if(  jz  ) {
		lchar = copy(tstr,1,1);
	} else {
		lchar = '';
	}
	if(  spell_lookup_word( lchar, jy )  ) {
		loop2xx:
			tstr2 = spell_next_match( tstr, jy );
			if(  (svl(tstr2) != 0)  ) {
				if(  (copy(tstr2,1,svl(forced_match)) == forced_match)  ) {
					tstr2 = char( return_int ) + tstr2;
					tof;
					if( !search_fwd( '%' + tstr2 + '$', 0 ) ) {
						eof;
						if(c_col > 1)
								down;
						put_line( tstr2 );
					}
					++jx;
				}
				goto loop2xx;
			}
	}
	RET;


check_replace:
		if(  op_type == 1  ) {
			tr = 1;
			RET;
		}
		RM('USERIN^XMENU /X=' + str(x+1) + '/Y=' + str(y + 3) +
			'/B=1/L=REPLACE WORD (<ESC> to abort)/M=Just this occurrence(SPELL^MSPLDUNKWN)All occurrences()');
		tr = return_int;
		ret;

replace_word:
	int  rx ;

	rx = 0;
caps_loop:
	if(  rx < svl(orig_word)  ) {
		if(  (str_char(orig_word,rx + 1) >= 'A') & (str_char(orig_word,rx + 1) <= 'Z')  ) {
			++rx;
			goto caps_loop;
		}
	}

	if(  rx == svl(orig_word)  ) {
		return_str = caps(return_str);
	} else {
		if(  (str_char(orig_word,1) >= 'A') & (str_char(orig_word,1) <= 'Z')  ) {
			return_str = caps(copy(return_str,1,1)) + str_del(return_str,1,1);
		}
	}
replace_word2:
	push_undo;
	del_chars( svl(tstr) );
	Insert_Mode = True;
	text(return_str);
	if(  wrap_stat  ) {
		word_wrap_line( FALSE, TRUE );
	}
	pop_undo;
	tstr = return_str;
	ret;


/* aux_window = window_number */
/* lstr = file_name */
add_word_to_aux:
	refresh = false;
	undo_stat = false;
	working;
	if(  cur_dic < 100  ) {
		ee = spell_close_dic( cur_dic );
	}
	if(  switch_win_id( aux_window ) == 0  ) {
		switch_window(window_count);
		create_window;
		window_attr = 1;
		if(  lstr != ''  ) {
			if(  get_path(lstr) == ''  ) {
				lstr = me_path + lstr;
			}
			load_file( lstr );
			if(  error_level != 0  ) {
				error_level = 0;
				file_name = lstr;
			}
		}
		aux_window = window_id;
	}
	tstr2 = tstr;
	call add_word_to_file;
	if(  cur_dic < 100  ) {
		save_file;
		delete_window;
		ee = spell_open_dic( lstr, cur_dic, 1, 2000 );
	}
	switch_win_id( work_window );
	undo_stat = true;
	ret;


/* assumes current file */
/* tstr2 is the word to insert */
/* puts word in file in alphebetical order */
/* eliminates duplicates */
add_word_to_file:
	tof;
	reg_exp_stat = true;
	if(  search_fwd('%' + copy(tstr2,1,1), 0)  ) {
	}
	while(  not(at_eof) & (get_line < tstr2)  ) {
		down;
	}
	goto_col(1);
	if(  get_line != tstr2  ) {
		if(  not(at_eof)  ) {
			cr;
			up;
		}
		put_line(tstr2);
	}
	ret;


abort_check:
	make_message('Spell check aborted, ' + str(word_count) + '  words checked.');
	delay(200);
	if(  check_key  ) {
		if(  key1 != 27  ) {
			push_key(key1,key2);
		}
	}
	goto exit;


check_complete:
		make_message('Spell check complete, ' + str(word_count) + ' words checked.');

exit:
	if(  op_type == 1  ) {
		if(  word_result == 1  ) {
			make_message('Word spelled correctly.');
		} else if(  word_result == 2  ) {
			make_message('Word replaced');
		} else if(  word_result == -1  ) {
		} else {
			make_message('');
		}
		jx = messages;
		messages = false;
		goto_mark;
		messages = jx;
	}

	refresh = false;

		/* close all dictionaries */
	if(  parse_int('/NCD=',mparm_str) == 1  ) {
		goto no_close_dicts;
	}
Close_Dictionaries:
	if(  switch_win_id( list_window )  ) {
    delete_window;
 //   window_attr = 0;
		SET_GLOBAL_INT('SPELL_LIST_WINDOW', 0);
	}
	if(  switch_win_id( aux1_window )  ) {
		delete_window;
		SET_GLOBAL_INT('SPELL_AUX2_WINDOW', 0);
	}
	if(  switch_win_id( aux2_window )  ) {
		delete_window;
		SET_GLOBAL_INT('SPELL_AUX2_WINDOW', 0);
	}
	if(  switch_win_id( ignore_window )  ) {
		delete_window;
		SET_GLOBAL_INT('SPELL_IGNORE_WINDOW',0);
	}
	ee = spell_close_dic( 1 );
	ee = spell_close_dic( 2 );
	ee = spell_close_dic( 3 );
	ee = spell_close_dic( 4 );
No_Close_Dicts:
  Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') - 1);
	switch_win_id( work_window );
	wrap_stat = t_word_wrap;
	return_int = 100;
	Insert_Mode = T_Insert_Mode;
	persistent_blocks = tpb;
	redraw;
}

/*******************************************************************************
																MULTI_EDIT MACRO

Name:  SPLSETF

Description: Retrieves spell check setup parameters from disk.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro SPLSETF {
		int  t2 = Window_Id;

		refresh = false;
/*
		error_level := 0;
		create_window;
		if error_level = 0 then
			return_str := 'SPELLSET.ME';
			RM('MakeUserPath /DF=1');
			load_file( return_str );
			update_status_line;
 */
			RM('SETCONFIG /DB=MECONFIG/T=SPELLSET.ME');
			set_global_str('SPELL_CHARS', 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_''');
			if(  (Return_Int)  ) {
/*
			if error_level = 0 then
 */
				Down;
				set_global_str( 'SPELL_COMMON_DIC', get_line );
				down;
				set_global_str( 'SPELL_MAIN_DIC', get_line );
				down;
				set_global_str( 'SPELL_AUX1_DIC', get_line );
				down;
				set_global_str( 'SPELL_AUX2_DIC', get_line );
				down;
				set_global_str( 'SPELL_IGNORE_W', get_line );
				down;
				set_global_str( 'SPELL_IGNORE_C', get_line );
				down;
				set_global_str( 'SPELL_PARMS', get_line );
				down;
/*
				if NOT(at_eof) THEN
 */
				Goto_Col(1);
				if(  ((at_eof == False) & (Cur_Char != '|12'))  ) {
					set_global_str( 'SPELL_CHARS', get_line );
				}
			} else {
/*
				error_level := 0;
 */
				set_global_str( 'SPELL_COMMON_DIC', 'COMMON.DIC' );
				set_global_str( 'SPELL_MAIN_DIC', 'ME.DIC' );
				set_global_str( 'SPELL_AUX1_DIC', 'AUXDIC.TXT' );
				set_global_str( 'SPELL_AUX2_DIC', 'AUXDIC2.TXT' );
				set_global_str( 'SPELL_IGNORE_W', '.@<' );
				set_global_str( 'SPELL_IGNORE_C', '^' );
				set_global_str( 'SPELL_PARMS', '/WS=1/SCRN=0/WW=0' );
			}
/*
			delete_window;
		else
			RM('MEERROR');
		end;
 */
		switch_win_id(t2);
		update_status_line;
}

macro SpellCheckSource {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SpellCheckSource

Description: Goes through a source file, from the current cursor position
	and spell checks all text that is in between single, or double quotes.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int BL1 = Block_Line1,
					BL2 = Block_Line2,
					BC1 = Block_Col1,
					BC2 = Block_Col2,
					BS = Block_Stat,
					tpb = persistent_blocks;

	persistent_blocks = TRUE;
	reg_exp_stat = TRUE;
	key1 = 0;
	return_int = 1;
	while(  Search_Fwd('[''"]',0)  ) {
		if(  (key1 == 27) | (return_int < 1)  ) {
			goto done;
		}
		RIGHT;
		Str_Block_Begin;
		if(  NOT(search_fwd(found_str,0))  ) {
			goto done;
		}
		block_End;
		Goto_Line( block_line1 );
		goto_col( block_col1 );
		refresh = TRUE;
		redraw;
		RM('SPELLCHK /T=2/NCD=1');
		goto_line(block_line2);
		goto_col(block_col2 + 2);
		reg_exp_stat = TRUE;
	}
DONE:
	RM('SPELLCHK /CD=1');
	if(  (Bs)  ) {
/* If there was a block defined, restore it.  Otherwise get rid of block. */
		Mark_Pos;
		Goto_Line(BL1);
		Goto_Col(BC1);
		if(  (Bs == 1)  ) {
			Block_Begin;
		} else if(  (Bs == 2)  ) {
			Col_Block_Begin;
		} else if(  (Bs == 3)  ) {
			Str_Block_Begin;
		}
		Goto_Line(BL2);
		Goto_Col(BC2);
		Block_End;
		Goto_Mark;
	} else {
		Block_Off;
	}
exit:
	persistent_blocks = tpb;
}


macro SPLSETUP {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:  SPLSETUP

Description: A setup interface for the spell checker.

Parameters:
						 /X= The upper left corner column coordinate
						 /Y= The upper left corner row coordinate

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
		int  x,y,
						T_Insert_Mode = Insert_Mode,
						Count,
						tw = Window_Id;

		refresh = false;

		y = parse_int('/Y=', mparm_str);
		if(  y == 0  ) {
			y = 2;
		}
		x = parse_int('/X=', mparm_str);
		if(  x == 0  ) {
			x = 2;
		}
		if(  global_str( 'SPELL_MAIN_DIC' ) == ''  ) {
			RM('splsetf');
		}

		Set_Global_Str('ISTR_1', global_str( 'SPELL_COMMON_DIC' ) );
		Set_Global_Str('IPARM_1','/QK=1/T=Common words dictionary............./C=1/W=24/ML=80');
		Set_Global_Str('ISTR_2', global_str( 'SPELL_MAIN_DIC' ) );
		Set_Global_Str('IPARM_2','/QK=1/T=Main dictionary...................../C=1/W=24/ML=80');
		Set_Global_Str('ISTR_3',global_str( 'SPELL_AUX1_DIC' ));
		Set_Global_Str('IPARM_3','/QK=1/T=Auxiliary dictionary 1.............../C=1/W=24/ML=80');
		Set_Global_Str('ISTR_4',global_str( 'SPELL_AUX2_DIC' ));
		Set_Global_Str('IPARM_4','/QK=21/T=Auxiliary dictionary 2.............../C=1/W=24/ML=80');
		Set_Global_Str('ISTR_5',global_str( 'SPELL_IGNORE_W' ));
		Set_Global_Str('IPARM_5','/QK=1/T=Ignore word after these chars......./C=1/W=20');
		Set_Global_Str('ISTR_6',global_str( 'SPELL_IGNORE_C' ));
		Set_Global_Str('IPARM_6','/QK=2/T=Ignore character after these chars../C=1/W=20');

		Set_Global_Int('IINT_7',parse_int('/WS=', global_str( 'SPELL_PARMS' )));
		Set_Global_Str('IPARM_7','/QK=9/T=Minimum word size.................../C=1/W=2/TP=1');
		Set_Global_Int('IINT_8',parse_int('/SCRN=', global_str( 'SPELL_PARMS' )));
		Set_Global_Str('ISTR_8','/T=YES/F=NO');
		Set_Global_Str('IPARM_8','/QK=1/T=Screen updating...................../C=1/W=3/TP=5');
		Set_Global_Int('IINT_9',parse_int('/WW=', global_str( 'SPELL_PARMS' )));
		Set_Global_Str('ISTR_9','/T=YES/F=NO');
		Set_Global_Str('IPARM_9','/QK=2/T=Reformat if Word Wrap is on........./C=1/W=3/TP=5');
		Set_Global_Str('ISTR_10',global_str( 'SPELL_CHARS' ));
		Set_Global_Str('IPARM_10','/QK=1/T=Valid word characters/C=1/W=40/ML=128');

		RM('UserIn^Data_In /H=SPELL^SETUPSPL/S=1/#=10/T=SPELL CHECK SETUP/Y=' + str(y));

		if(  return_int  ) {
			Set_Global_Str('SPELL_COMMON_DIC', global_str('ISTR_1'));
			Set_Global_Str('SPELL_MAIN_DIC', global_str('ISTR_2'));
			Set_Global_Str('SPELL_AUX1_DIC', global_str('ISTR_3'));
			Set_Global_Str('SPELL_AUX2_DIC', global_str('ISTR_4'));
			Set_Global_Str('SPELL_IGNORE_W', global_str('ISTR_5'));
			Set_Global_Str('SPELL_IGNORE_C', global_str('ISTR_6'));
			Set_Global_Str('SPELL_CHARS', global_str('ISTR_10'));
			Set_Global_Str('SPELL_PARMS', '/WS=' + str(global_int('IINT_7')) + '/SCRN=' + str(global_int('IINT_8'))
						+ '/WW=' + str(global_int('IINT_9'))
					);

 /* now save the configuration */
			RM('SETCONFIG /DB=MECONFIG/T=SPELLSET.ME');
			if(  (Return_Int)  ) {
/* If there is existing configuration options, delete them */
				Down;
				Goto_Col(1);
				while(  ((At_Eof == False) & (Cur_Char != '|12'))  ) {
					Del_Line;
				}
				Up;
			} else {
				Eof;
				if(  (C_Col > 1)  ) {
					Down;
				}
				Put_Line('|12SPELLSET.ME');
			}
			Insert_Mode = True;
			Count = 0;
			while(  (Count < 6)  ) {
				Eol;
				Cr;
				++Count;
				put_line( global_str('ISTR_' + Str(Count)) );
			}
			Eol;
			Cr;
			put_line( global_str('SPELL_PARMS') );
			Eol;
			Cr;
			put_line( global_str('SPELL_CHARS') );
/*
			error_level := 0;
			create_window;
			if error_level = 0 then
				return_str := 'SPELLSET.ME';
				RM('MakeUserPath');
				file_name := return_str;
				put_line( global_str('SPELL_COMMON_DIC') );
				down;
				put_line( global_str('SPELL_MAIN_DIC') );
				down;
				put_line( global_str('SPELL_AUX1_DIC') );
				down;
				put_line( global_str('SPELL_AUX2_DIC') );
				down;
				put_line( global_str('SPELL_IGNORE_W') );
				down;
				put_line( global_str('SPELL_IGNORE_C') );
				down;
				put_line( global_str('SPELL_PARMS') );
				down;
				put_line( global_str('SPELL_CHARS') );
				down;
				save_file;
				delete_window;
			else
				RM('MEERROR');
			end;
 */
		}
		switch_win_id(tw);
		update_status_line;
		Insert_Mode = T_Insert_Mode;
		return_int = 0;
}
