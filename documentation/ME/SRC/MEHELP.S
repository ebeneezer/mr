//+++keyword+++ "@(#)%v %n, %f, %w"
// "@(#)1 MEHELP.S, 15-Jun-91,2:14:38, LDH"
/*******************************************************************************
													 MULTI-EDIT MACRO FILE

MEHELP					- The main Multi-Edit help system
HELP_LIST_MARKS	- Brings up a list of currently defined help book marks.
SCREENMRK				- Allows any part of the screen to be marked, and have
									that section of the screen copied to the scrap buffer.
SCREENCUT				- Captures the section of the screen defined by SCREENMRK and
									sends it to the buffer or printer.
GET_CONTEXT			- Supports context sensitive help.

						 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro mehelp trans2
{
/*******************************************************************************
															 MULTI-EDIT MACRO
Name: MEHELP

Description:  The Multi-Edit Help system

Parameters:
							/X=n		X coordinate
							/Y=n		Y coordinate
							/W=n		Width
							/L=n		Length
							/T=str  Title
							/F=n		Filename
 							/LK=str Index
							/CX=1		Enables file/extension context sensitivity.
							/TO=n		Top offset (number of lines from top of chapter to start
												displaying).
							/POP=1  Pop position

						 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
//+++keyword+++ "@(#)%v  %n, %f, %w"
	str  tstr[100], tstr2[100], cur_file[80], event_str[20], help_path[80] ;
	str  extfil[100], extlnk[80], fname[80], hstr[100];                         /*  ldh  */
  str extpath[100];                                                   // ldh4
	int  old_sb, jx, jy, jz, x, y, w, l, tbc, tx, ty, h_v_color,
					 trefresh, done, event_count, cl, need_new_bar, scroll_bar,
					 trepeat, need_event_draw, old_x, old_y, min_y, max_y,
					 tfkey_row, tstatus_row, file_help_mode, need_search_display,
           hlc_time, extloc,                                          // ldh6
           skip_err = FALSE                                           // ldh6
				;
  str fstr[100];                                                      // ldh6

  if(  help_open  ) {
		rm('MEERROR^MessageBox /B=1/T=UNABLE TO ACCESS HELP/M=Help system already in use.');
		goto total_exit;
	}
	Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') + 1);
	trefresh = refresh;
	refresh = false;
	event_count = 0;

	file_help_mode = parse_int('/CX=', mparm_str );
	if(  file_help_mode != 0  ) {

/* <<<Added 03-22-90 ldh *************************************************ldh2**/
		Return_Str = Global_Str('!HELP_WORD!');
		RM('Get_Context');
		hstr = CAPS(Return_Str);
/******************************************************************** Added>>> */

		fname = truncate_extension( File_name ) + '.HLP';                  /*  ldh  */
		RM('Find_Ext /EXT=.' + get_extension(file_name));                   /*  ldh  */
//		extfil = Parse_Str('HLP=', Global_Str(Return_Str));               /*  ldh  */
		extpath = Parse_Str('HLP=', Global_Str(Return_Str));             // ldh4
    extloc = 0;                                                       // ldh4
    call Parse_File;                                                  // ldh4
    if (hstr == '') {                                                 // ldh5
      Return_Int = 1;                                                 // ldh5
      while (Return_Int)                                              // ldh5
        call Next_Path;                                               // ldh5
    }
//     jx = xpos('^', extfil, 1);                                     // ldh6
//     if(  jx != 0  ) {                                              // ldh6
//       extlnk = str_del( extfil, 1, jx);                            // ldh6
//       extfil = copy( extfil, 1, jx - 1 );                          // ldh6
//     }                                                              // ldh6
	}

/* Setup the help path */
	help_path = global_str('!HELP_PATH!');
	if(  help_path == ''  ) {
		if(  (file_attr(me_path + 'HELP') & $10) != 0  ) {
			help_path = me_path + 'HELP' + ';' + me_path;
		} else {
			help_path = me_path;
		}
	}
	if(  xpos('<', help_path, 1)  ) {
		return_str = help_path;
		RM('XLATECMDLINE');
		help_path = return_str;
	}
	set_global_str('!HELP_PATH!', help_path);
	tx = wherex;
	ty = wherey;
	trepeat = mou_repeat;
	tfkey_row = fkey_row;
	tstatus_row = status_row;
	mou_repeat = TRUE;
	need_search_display = FALSE;

	x = Parse_Int('/X=', mparm_str);
	y = Parse_Int('/Y=', mparm_str);
	w = Parse_Int('/W=', mparm_str);
	l = Parse_Int('/L=', mparm_str);

	event_str =  '@EV' + Str(Global_Int( 'MENU_LEVEL' )) + '#';

	min_y = 1 + (status_row == 1) + (menu_bar_row != 0);
	max_y = screen_length - (status_row == screen_length);
	fkey_row = screen_length - (status_row == screen_length);

	if(  x == 0  ) {
		x = 1;
	}
	if(  y == 0  ) {
		y = min_y;
	}
	if(  w == 0  ) {
		w = Screen_width - x;
	}
	if(  l == 0  ) {
		l = max_y - y - 1;
	}
	if(  (y + l) > (max_y - 2)  ) {
		y = max_y - l - 1;
	}
	if(  (x + w) > (screen_width)  ) {
		x = screen_width - w - 2;
	}

/* Experimental mods to get max lines: very quick and dirty
l := L + 3;
--Y;
FKey_row := 0;
Status_row := 0;
 */
	h_v_color = ((h_b_color & $07) << 4) + (h_b_color >> 4);
	tbc = box_count;
	tstr = parse_str('/T=', mparm_str);
	if(  tstr == ''  ) {
		tstr = 'HELP';
	}

/* Experimental mods to get max lines
put_box( x,y,x + w,y + l, 0, h_b_color, tstr , false );
Write('Ä', x + 31,y,0, h_b_color);
 */

	put_box( x,y,x + w + 2,y + l + 1, 0, h_b_color, tstr , true );

	push_labels;
	flabel( 'Contns',2, - 1);
	flabel( 'Index',3, - 1);
	flabel( 'Paste',4, -1);
	flabel( 'BkMark',5, -1);
	flabel( 'Marks',15, -1);
  flabel( 'Search',6, - 1);
	flabel( 'LdFile',7, - 1);                                           /*  ldh  */

	if(  file_help_mode  ) {
		flabel( 'FilHlp',8, - 1);                                           /*  ldh  */
		flabel( 'ExtHlp',9, - 1);                                           /*  ldh  */
	}
	flabel( 'MEHlp ',10, - 1);                                          /*  ldh  */
	flabel( 'NxtSrc',26, - 1);
	flabel( 'SrcFil',36, - 1);

	help_start;
	help_size_window( x, y, x + w, y + l );

	if(  parse_int('/EXTHELP=',mparm_str)  ) {
		call file_ext_help;
	} else if(  parse_int('/POP=', mparm_str)  ) {
		call help_pop_pos;
	} else {
		tstr = parse_str('/LK=', mparm_str);
		if(  xpos('^', tstr, 1) == 0  ) {
			tstr2 = parse_str('/F=', mparm_str);
			if(  (file_help_mode) & (tstr2 == '') & (tstr == '')  ) {                              /*  ldh  */
				call file_hlp;                                                  /*  ldh  */
				goto Do_Help;                                                   /*  ldh  */
			} else {                                                              /*  ldh  */
				call open_file2;
				if(  error_level != 0  ) {
					RM( 'MEERROR' );
					goto exit;
				}
			}                                                              /*  ldh  */
		}
		if(  tstr == ''  ) {
			tstr = '*';
		}
		call select_link2;
		help_current_offset = parse_int('/TO=', mparm_str );
	}
Do_Help:

	done = false;
	Set_Global_Str(event_str + '1',
			'/T=Done/KC=<ESC>/W=9/K1=27/K2=1/R=0');
	Set_Global_Str(event_str + '2',
			'/T=Back-track/KC=<BackSpace>/W=21/K1=08/K2=14/R=1');
	event_count = 2;
	RM('USERIN^CheckEvents /M=4/G=' + event_str + '/#=' + str(event_count) +
				'/X=' + str( x ) + '/Y=' + str( y + l ) + '/W=' + str( w ));

	Need_Event_Draw = TRUE;

loop:
	Set_Virtual_Display;
	if(  need_search_display  ) {
		need_search_display = FALSE;
		help_display_search;
		if(  error_level != 0  ) {
			error_level = 0;
			help_update;
			help_hi_link;
		}
	} else {
		help_update;
		help_hi_link;
	}
	if(  need_event_draw  ) {
		call draw_events;
	}
	call vertical_scroll_bar;
	Update_Virtual_Display;
	Reset_Virtual_Display;
	if (error_level != 0) {
		jx = error_level;
		rm('MEERROR');
		error_level = 0;
		if(  jx == 8001  ) {
			call help_pop_pos;
			if(  error_level != 0  ) {
				RM('MEERROR');
				done = TRUE;
				goto exit;
			}
			help_update;
			help_hi_link;
		}
	}
	update_status_line;
	while(  check_key == 0  ) {
		old_x = mou_last_x;
		old_y = mou_last_y;
		Mou_Check_Status;
		if(  ((old_x != mou_last_x) | (old_y != mou_last_y))  ) {
			call check_links;
		}
	}

	help_lo_link;
	if(  key1 == 0  ) {
		if(  key2 == 72  ) {
			help_up;
		} else if(  key2 == 80  ) {
			help_down;
		} else if(  key2 == 77  ) {
			help_right;
		} else if(  (key2 == 75) | (key2 == 15)  ) {
			help_left;
		} else if(  key2 == 71  ) {
			help_home;
		} else if(  key2 == 79  ) {
			help_end;
		} else if(  key2 == 116  ) {
			help_scroll_right;
		} else if(  key2 == 115  ) {
			help_scroll_left;
		} else if(  key2 == 141  ) {
			help_scroll_up;
		} else if(  key2 == 145  ) {
			help_scroll_down;
		} else if(  key2 == 81  ) {
			help_pgdn;
		} else if(  key2 == 73  ) {
			help_pgup;
		} else if(  key2 == 73  ) {
			help_pgup;
		} else if(  key2 == 59  ) {
			call help_push_pos;                                             /*  ldh  */
			tstr = 'ACI_HYPE^*';
			call select_link2;
		} else if(  key2 == 60  ) {
			call help_push_pos;                                             /*  ldh  */
			tstr = '*';
			call select_link2;
			help_update;
			if(  error_level != 0  ) {
				call help_pop_pos;
				tstr = 'ME^*';
				call select_link2;
			}
		} else if(  key2 == 61  ) {
			call help_push_pos;                                             /*  ldh  */
			tstr = 'INDEX';
			call select_link2;
			help_update;
			if(  error_level != 0  ) {
				call help_pop_pos;
				tstr = 'ME^INDEX';
				call select_link2;
			}
		} else if(  key2 == 62  ) { /* paste */
			write( ' MARKING ', working_col, status_row, 0, working_color );
			draw_char( 205, x + 1, Y + l, H_B_color, w - 1);
			RM('ScreenMrk');
			Need_Event_Draw = TRUE;
			update_status_line;
		} else if(  key2 == 63  ) {
			call HELP_BOOK_MARK;
		} else if(  key2 == 88  ) {
			call HELP_LIST_MARKS;
		} else if(  key2 == 64  ) {
			jx = 0;
			call prompt_and_search;
		} else if(  key2 == 65  ) {                                              /*  ldh  */
			/* 09-10-90 10:15am */
			Return_Str = cur_file;
			RM('MEUTIL1^File_Prompt /T=Help filename to load');
			if(  return_int  ) {
				tstr = Caps( Return_Str );
				if(  get_extension(tstr) == ''  ) {
					tstr = tstr + '.HLP^*';
				} else {
					tstr = tstr + '^*';
				}
				CALL help_push_pos;
				CALL select_link2;
			}
    } else if(  key2 == 66  ) {                                  /*  ldh  */
			if(  file_help_mode  ) {
        call help_push_pos;                                      /*  ldh  */
        call file_hlp;                                           /*  ldh  */
			}
    } else if(  key2 == 67  ) {                                  /*  ldh  */
			if(  file_help_mode  ) {
        call help_push_pos;                                      /*  ldh  */
        call file_ext_help;                                      /*  ldh  */
			}
    } else if(  key2 == 68  ) {                                  /*  ldh  */
      call help_push_pos;                                        /*  ldh  */
			tstr = 'ME^*';                                             /*  ldh  */
      call select_link2;                                         /*  ldh  */
		} else if(  key2 == 99  ) {
			working;
			help_repeat_search;
			need_new_bar = TRUE;
			need_search_display = TRUE;
			if(  error_level != 0  ) {
				RM('MEERROR^MessageBox /M=No more occurrences found.');
				error_level = 0;
			}
		} else if(  key2 == 109  ) {
			jx = 1;
			call prompt_and_search;
		} else if(  key2 == 250  ) {
			call check_mouse_pos;
		}
	} else if(  key1 == 09  ) {
		Help_Right;
	} else if(  key1 == 08  ) {
		call help_pop_pos;
	} else if(  key1 == 27  ) {
		done = TRUE;
	} else if(  key1 == 13  ) {
		call select_link;
	} else {
		jx = help_current_link;
		help_find_index( caps(char(key1)) );
		if(  error_level == 8012  ) {
			error_level = 0;
		} else {
			if(  help_current_link == jx  ) {
				call select_link;
			}
		}
	}
	if(  done  ) {
		goto exit;
	}

	if(  error_level != 0  ) {
		run_macro('MEERROR');
		error_level = 0;
	}

	goto loop;

file_hlp:                                                             /*  ldh  */
	tstr2 = fname;                                                     /*  ldh  */
	call open_file2;                                                    /*  ldh  */
	if(  error_level != 0  ) {                                            /*  ldh  */
		error_level = 0;                                                 /*  ldh  */
		call file_ext_help;                                               /*  ldh  */
	} else {                                                                /*  ldh  */
		tstr = hstr;                                                     /*  ldh  */
		call select_link2;                                                /*  ldh  */
		help_update;                                                      /*  ldh  */
		if(  error_level != 0  ) {                                          /*  ldh  */
			error_level = 0;                                               /*  ldh  */
			tstr = '*';                                                    /*  ldh  */
			call select_link2;                                              /*  ldh  */
			help_update;                                                    /*  ldh  */
			if(  error_level != 0  ) {                                        /*  ldh  */
				error_level = 0;                                             /*  ldh  */
				call file_ext_help;                                           /*  ldh  */
			}                                                            /*  ldh  */
		}                                                              /*  ldh  */
	}                                                                /*  ldh  */
	ret;                                                                /*  ldh  */

file_ext_help:                                                        /*  ldh  */
	if(  extfil == ''  ) {                                                 /*  ldh  */
    call next_path;                                                   // ldh4
    if (Return_Int)                                                   // ldh4
      goto file_ext_help;                                             // ldh4
    else                                                              // ldh4
			tstr = 'ME^*';                                               		/*  ldh  */
	} else {                                                                /*  ldh  */
		tstr2 = extfil;                                                  /*  ldh  */
		call open_file2;                                                  /*  ldh  */
		if(  error_level != 0  ) {                                          /*  ldh  */
			error_level = 0;                                               /*  ldh  */
      call next_path;                                                 // ldh4
      if (Return_Int)                                                 // ldh4
        goto file_ext_help;                                           // ldh4
      else                                                            // ldh4
				tstr = 'ME^*';                                             /*  ldh  */
		} else {                                                              /*  ldh  */
      if (copy(extlnk, svl(extlnk), 1) == "&") {
        fstr = "% *" + make_literal(hstr) + "$| ";                    // ldh6
      }
      else                                                            // ldh6
        fstr = hstr;                                                  // ldh6
    }
    tstr = extlnk + fstr;                                             // ldh6
  }                                                                   // ldh4
  skip_err = TRUE;                                                    // ldh6
  call select_link2;                                                  // ldh
  skip_err = FALSE;                                                   // ldh6
  if (!error_level)                                                   // ldh6
    help_update;                                                      // ldh
  if (error_level) {                                                  // ldh
    error_level = 0;                                                  // ldh
    call next_path;                                                   // ldh4
    if (Return_Int)                                                   // ldh4
      goto file_ext_help;                                             // ldh4
//     else                                                           // ldh6
//       tstr = '*';                                                  // ldh6
    tstr = 'INDEX';                                                   // ldh6
    skip_err = TRUE;                                                  // ldh6
    call select_link2;                                                // ldh
    help_update;                                                      // ldh6
    if (error_level) {                                                // ldh6
      error_level = 0;                                                // ldh6
      tstr = '*';                                                     // ldh6
      call select_link2;                                              // ldh6
      skip_err = FALSE;                                               // ldh6
    }                                                                 // ldh6
    if (!error_level)                                                 // ldh6
      rm('MEERROR^MessageBox /B=1/M=Search string "' +                // ldh6
            copy(hstr, jx + 1, 255 ) + '" not found.');               // ldh6
    skip_err = FALSE;                                                 // ldh6
    help_update;                                                      // ldh
    if (error_level) {                                                // ldh
      error_level = 0;                                                // ldh
      call help_pop_pos;                                              // ldh
      tstr = 'ME^*';                                                  // ldh
      call select_link2;                                              // ldh
    }                                                                 // ldh
  }                                                                   // ldh
  ret;                                                                // ldh

next_path:                                                            // ldh4
  if (extloc > LENGTH(extpath))                                       // ldh4
    Return_Int = 0;                                                   // ldh4
  else                                                                // ldh4
  {
    call parse_file;                                                  // ldh4
    Return_Int = 1;                                                   // ldh4
  }                                                                   // ldh4
  ret;                                                                // ldh4


parse_file:                                                           // ldh4
  jx = xpos(';', extpath, extloc + 1);                                // ldh4
  if (jx == 0)                                                        // ldh4
    jx = svl(extpath) + 1;                                            // ldh4
  extfil = copy(extpath, extloc + 1, jx - extloc - 1);                // ldh4
  extloc = jx;                                                        // ldh4
  extlnk = "";                                                        // ldh6
  jx = xpos('^', extfil, 1);                                          // ldh6
  if (jx) {                                                           // ldh6
    extlnk = str_del( extfil, 1, jx);                                 // ldh6
    extfil = copy(extfil, 1, jx - 1);                                 // ldh6
  }                                                                   // ldh6
  ret;                                                                // ldh4

prompt_and_search:
  Create_Global_Str('!H_ISTR_1', fstr);                               // ldh6
	Create_Global_Str('!H_IPARM_1','/L=1/W=50');
	Set_Global_Int('!H_IINT_2',global_int("HELP_REG_EXP"));																// ldh4
	Create_Global_Str('!H_IPARM_2','/QK=9/T=Regular expressions/L=3/C=2/TP=13');
	RM( 'USERIN^DATA_IN /PRE=!H_/#=2/T=INPUT SEARCH STR');
	set_global_int("HELP_REG_EXP", global_int('!H_IINT_2'));
	if(  return_int  ) {
		return_str = Global_Str('!H_ISTR_1');
		jy = help_current_link;
		working;
		reg_exp_stat = global_int("HELP_REG_EXP");
		help_search(return_str, jx);
		reg_exp_stat = true;
		need_new_bar = TRUE;
		if(  error_level == 0  ) {
			need_search_display = TRUE;
		} else {
			RM('MEERROR^MessageBox /M=Search string"' + return_str + '" not found.');
			help_current_link = jy;
			error_level = 0;
		}
	}
	Set_Global_Str('!H_ISTR_1','');
	Set_Global_Int('!H_IINT_2',0);
	RET;

select_link:
	if(  help_link_count > 0  ) {
		working;
		call help_push_pos;
		tstr = help_link( help_current_link );
select_link2:
		if(  str_char(tstr,1) == '<'  ) {
			tstr = str_del( tstr, 1, 1 );
			save_box( 1, 1, screen_width, screen_length );
			shell_to_dos( tstr, true );
			restore_box;
			goto select_link_done;
		} else if(  str_char( tstr, 1 ) == '>'  ) {
			tstr = str_del( tstr, 1, 1 );
			RM( tstr );
			goto select_link_done;
		} else if (jx = xpos('^', tstr, 1)) {
			tstr2 = copy( tstr, 1, jx - 1 );
			tstr = str_del( tstr, 1, jx );
			call open_file2;
			if(  error_level != 0  ) {
				run_macro('MEERROR');
				error_level = 0;
				call help_pop_pos;
				ret;
			}
		}
		jx = xpos( '&', tstr, 1 );
		if(jx ==  0)
		{
			reg_exp_stat = 0;
			jx = xpos( '%', tstr, 1 );
		}
		else
			reg_exp_stat = true;
		if(  jx != 0  ) {
			if(  JX > 1  ) {
				help_select( caps(copy(tstr,1,jx - 1)) );
			}
			if(  error_level == 0  ) {
				help_search( copy(tstr,jx + 1, 255 ), 0);
				if(  error_level == 0  ) {
					need_search_display = TRUE;
        } else if (!skip_err) {                                       // ldh6
          RM('MEERROR^MessageBox /B=1/M=Search string "' + copy(tstr,jx + 1, 255 ) + '" not found.');
					error_level = 0;
				}
			}
		} else {
			help_select( caps(tstr) );
		}
		reg_exp_stat = true;
	select_link_done:
		need_new_bar = TRUE;
	}
	ret;

help_pop_pos:
	jx = global_int('@HSTACK' );
	if(  jx > 0  ) {
		working;
		tstr2 = parse_str('/F=',global_str('@HSTACK' + str(jx) ));
		if(  tstr2 != cur_file  ) {
			call open_file2;
		}
		tstr = global_str('@HSTACK' + str(jx) );
		help_select( caps(parse_str('/C=', tstr )));
		help_current_offset = parse_int('/O=', tstr );
		help_current_link = parse_int('/L=', tstr );

		set_global_str('@HSTACK' + str(jx), '' );
		--jx;
		set_global_int('@HSTACK', jx );
		need_new_bar = TRUE;
	} else {                                                                /*  ldh1  */
		tstr2 = 'ME.HLP';                                                /*  ldh2  */
		call open_file2;                                                  /*  ldh2  */
		if(  error_level != 0  ) {                                          /*  ldh2  */
			RM( 'MEERROR' );                                                /*  ldh2  */
			goto exit;                                                      /*  ldh2  */
		}                                                              /*  ldh2  */
		tstr = '*';                                                      /*  ldh2  */
		call select_link2;                                                /*  ldh1  */
	}
	ret;

set_pos:
	return_str = help_current_chapter;
	call double_slash;
	return_str =   '/F=' + cur_file +
									'/O=' + str(help_current_offset) +
									'/C=' + return_str  +
									'/L=' + str(help_current_link);
	ret;

help_push_pos:
	jx = global_int('@HSTACK' );
	call set_pos;

	if(  return_str != Global_Str( '@HSTACK' + str(jx)) ) {
		if(  jx > 20  ) {
			return_int = 1;
			RM('USERIN^DELETEITEM /G=@HSTACK/#=' + str( jx ));
		} else {
			++jx;
		}
		Set_Global_Str('@HSTACK' + str(jx), return_str);
		set_global_int('@HSTACK', jx );
	}
	ret;

open_file:
	call help_push_pos;
open_file2:
	working;
	need_new_bar = TRUE;
	cur_file = tstr2;
	if(  cur_file == ''  ) {
		cur_file = 'ME.HLP';
	}

/* Check for compressed file */
	tstr2 = get_extension( cur_file );
	cur_file = truncate_extension( cur_file );


	if ((tstr2 == 'HLP') || (tstr2 == '')) {
		Return_Str = Get_Path(cur_file);                               /*  ldh1  */
		if(  Return_Str == ''  ) {                                     /*  ldh1  */
			return_str = help_path;
			RM('PATHSEARCH /F=' +  Truncate_Path(cur_file) + '.HL?');    /*  ldh1  */
		} else if (file_exists(cur_file + '.HL?')) {                   /*  ldh3  */
			Return_Int = TRUE;                                           /*  ldh3  */
			Return_Str = fexpand(cur_file + '.HL?');                     /*  ldh3  */
		} else {                                                       /*  ldh3  */
			Return_Int = FALSE;                                          /*  ldh3  */
		}                                                              /*  ldh3  */
		if (return_int) {
			cur_file = truncate_extension( return_str );
			if (first_file( cur_file + '.HLZ' ) == 0) {
				hlc_time = Last_File_Time;
				tstr2 = 'HLZ';
				if (first_file( cur_file + '.HLP' ) == 0) {
					if (last_file_time > hlc_time) {
						tstr2 = 'HLP';
					}
				}
				cur_file = fexpand( cur_file + '.' + tstr2 );
				goto DO_OPEN;
			} else {
				if (file_exists( cur_file + '.HLP')) {
					cur_file = cur_file + '.HLP';
					goto DO_OPEN;
				}
			}
		}
		call HELP_ERROR;
//    error_level = 8003                                             /*  ldh1  */
		ret;
	}

	cur_file = truncate_extension( cur_file ) + '.' + tstr2;
	Return_Str = Get_Path(cur_file);                                 /*  ldh1  */
	if(  Return_Str == ''  ) {                                       /*  ldh1  */
		return_str = help_path;
	}                                                                /*  ldh1  */
	RM('PATHSEARCH /F=' +  Truncate_Path(cur_file));                 /*  ldh1  */
	if (return_int) {
		cur_file = return_str;
		goto do_open;
	}

	call HELP_ERROR;
//  error_level = 8002;                                              /*  ldh1  */

	ret;

do_open:
	help_open_file( cur_file );
	call draw_top_name;
	ret;

check_mouse_pos:
	if(  (Mou_Last_Y == Fkey_Row)  ) {
		RM( 'MOUSE^MouseFkey' );
	} else {
		if(  (Mou_Last_X < x) | (Mou_Last_Y < y) | (Mou_Last_X > (x + w)) |
				(Mou_Last_Y > (y + l))  ) {
			ret;
		}
		if(  (Mou_Last_X == (x + w))  ) {
			if(  (scroll_bar)  ) {
				if(  (Mou_Last_Y == (y + 1))  ) {
					help_scroll_up;
				} else if(  (Mou_Last_Y == (y + l - 1))  ) {
					help_scroll_down;
			/*  ELSIF (Mou_Last_Y > (y + 1 )) AND (Mou_Last_Y < (y + l - 1)) THEN
					IF (Mou_Last_Y = (y + l - 2)) THEN
							jx := help_chapter_length - (l - 1);
					ELSE
						jx := ((((help_chapter_length - (l - 1)) * 1000) / (l - 3)) * (Mou_Last_Y - Y - 2)) / 1000;
					END;
					IF (help_current_offset + (l - 1)) > help_chapter_length THEN
						jx := help_chapter_length - (l - 1);
					END;
					help_select( help_current_chapter );
					help_current_offset := jx;
				END;
			 */
				} else if(  (Mou_Last_Y > ( y + 2 + old_sb))  ) {
					help_pgdn;
				} else if(  (Mou_Last_Y < ( y + 2 + old_sb))  ) {
					help_pgup;
				} else if(  (Mou_Last_Y == ( y + 2 + old_sb))  ) {
					Mou_Set_Limits(x + w,y + 2,x + w, y + l - 2);
					old_y = mou_last_y;
					du3:
						Mou_Check_Status;
						if(  ((Mou_Last_Status & 1) != 0)  ) {
							if(  old_y != mou_last_y  ) {
								old_y = mou_last_y;
								if(  (y + 2 + old_sb) != mou_last_y  ) {
									if(  (Mou_Last_Y == (y + l - 2))  ) {
											jx = help_chapter_length - (l - 1);
									} else {
										jx = ((((help_chapter_length - (l - 1)) * 1000) / (l - 3)) * (Mou_Last_Y - Y - 2)) / 1000;
									}
									if(  (help_current_offset + (l - 1)) > help_chapter_length  ) {
										jx = help_chapter_length - (l - 1);
									}
									help_select( caps(help_current_chapter) );
									help_current_offset = jx;
									help_update;
									call vertical_scroll_bar;
								}
							}
							goto du3;
						}
					Mou_Set_Limits(1,1,screen_width, screen_length);

				}
			}
		} else {
			call check_links;
			if(  return_int  ) {
				call select_link;
			}
			RM('USERIN^CheckEvents /M=1/G=' + event_str + '/#=' + str(event_count));
			if(  return_int != 0  ) {
				return_int = parse_int('/R=', return_str );
				if(  return_int == 0  ) {
					done = TRUE;
				} else if(  return_int == 1  ) {
					call help_pop_pos;
				}
				need_event_draw = TRUE;
			}
		}
	}
	ret;

	/* see if the mouse cursor is on a link */
check_links:
	return_int = 0;
	jx = 0;
	while(  jx < help_link_count  ) {
		++jx;
		jy = help_link_x(jx);
		if(  (Mou_Last_Y == help_link_y(jx)) &
			(Mou_Last_X >= jy) &
			(Mou_Last_X < (jy + help_link_w(jx)))  ) {
			if(  jx != help_current_link  ) {
				help_lo_link;
				help_current_link = jx;
			}
			help_hi_link;
			return_int = 1;
			jx = help_link_count;
		}
	}
	ret;

	int  dt1 ;
draw_top_name:
	tstr2 = cur_file;
	dt1 = (( w / 2 ) - (svl(tstr2) / 2));
	if(  dt1 < 0  ) {
		tstr2 = truncate_path(tstr2);
		dt1 = (( w / 2 ) - (svl(tstr2) / 2));
	}
	if(  dt1 > 0  ) {
		draw_char( 196, x + 1, y, h_b_color, dt1 );
	}
	write( tstr2, x + dt1 + 1, y, 0, h_b_color );
	if(  dt1 > 1  ) {
		draw_char( 196, x + dt1 + svl(tstr2) + 1, y, h_b_color, dt1 - 1 );
	}
	ret;

draw_events:
	RM('USERIN^CheckEvents /M=2/G=' + event_str + '/#=' + str(event_count));
	need_event_draw = FALSE;
	ret;

HELP_ERROR: {
/*  after not finding a help file, this routine sets error_level to the correct
value to provide a more meaningful error message to the user
8002 - Help file not found
8003 - Help path not found
8037 - No help files found in help path

RETURN_STR is expected to contain the path that was searched

*/

	str fpath[120] = "";

	jy = 0;
	error_level = 8003;
	goto ERROR_FIRST_TIME;

HELP_ERROR_LOOP:
	if (jy > length(return_str)) {
		goto HELP_ERROR_EXIT;
	}
	CALL parse_path;

ERROR_FIRST_TIME:
	if ((file_exists(fpath)) && (file_attr(fpath) & $10)) {
		error_level = 8037;
		if (first_file(fpath + "\*.HL?") == 0) {
			error_level = 8002;
			goto HELP_ERROR_EXIT;
		}
	}
	goto HELP_ERROR_LOOP;

	Parse_Path:
		jx = XPos( ';', return_str, jy + 1 );
		if (jx == 0) {
			jx = LENGTH( return_str ) + 1;
		}
		fpath = Copy( return_str, jy + 1, jx - jy - 1 );
		if (str_char(fpath,SVL(fpath)) == '\') {
			fpath = copy(fpath,1,svl(fpath) - 1);
		}
		jy = jx;
		RET;

HELP_ERROR_EXIT:
	RET;
}

Vertical_Scroll_Bar:
	if(  need_new_bar  ) {
		call set_scroll_bar;
	}
	if(  scroll_bar  ) {
		jz = l - 3;

		jx = ((help_current_offset + 1) / jy);

		jy = help_chapter_length - (l - 1);
		if(  jy < 1  ) {
			jy = 1;
		}
		cl = help_current_offset;

		jx = ((((jz * 1000) / jy) * (cl + 1)) / 1000);

		if(  (cl == 0)  ) {
			jx = 0;
		}
		if(  (jx == 0) & (cl > 0)  ) {
			++jx;
		}
		if(  jx > (jz - 1)  ) {
			jx = jz - 1;
		}
		if(  (jx == (jz - 1)) & (cl < jy)  ) {
		 --jx;
		}
		if(  jx != old_sb  ) {
			if(  (old_sb >= 0)  ) {
				draw_char( 176, x + w, y + 2 + old_sb, h_b_color, 1 );
			}
			draw_char( 178, x + w, y + 2 + jx, h_b_color, 1 );
			old_sb = jx;
		}
	}
	ret;

set_scroll_bar:
	jy = 0;
	need_new_bar = 0;
	if(  help_chapter_length < l  ) {
		scroll_bar = false;
		while(  jy < (l - 1)  ) {
			++jy;
			draw_char( 186, x + w, y + jy, h_b_color, 1 );
		}
	} else {
		scroll_bar = true;
		while(  jy < (l - 2)  ) {
			++jy;
			draw_char( 176, x + w, y + 1 + jy, h_b_color, 1 );
		}
		draw_char( 24, x + w, y + 1, h_v_color, 1 );
		draw_char( 25, x + w, (y + l) - 1, h_v_color, 1 );
		old_sb = -1;
	}
	ret;

	int  djx ;
double_slash:
	djx = 1;
	while(  djx > 0  ) {
		djx = xpos( '/', return_str, djx );
		if(  djx != 0  ) {
			return_str = str_ins( '/', return_str, djx );
			++djx;
			++djx;
		}
	}
	ret;

	int  owin ;
help_book_mark:
	owin = window_id;
	refresh = false;
	RM('SETCONFIG /DB=MECONFIG/T=HELPMARK.DB');
	if(  return_int  ) {
		goto_col(1);
		down;
		while( (get_line != '****START****') && (cur_char != '|12') && NOT(at_eof)  ) {
			down;
		}
		if( at_eol || (cur_char == '|12'))
				up;
		eol;
		cr;
		put_line(	'CT=' + remove_space(help_chapter_title) +
										'F=' + truncate_extension(truncate_path(cur_file)) +
										'O=' + str(help_current_offset) +
										'C=' + help_current_chapter  +
										'L=' + str(help_current_link)
							);
	}
	switch_win_id( owin );
	ret;

help_list_marks:
	RM('HELP_LIST_MARKS /FROMHELP=1');
	if(  return_int  ) {
		working;
		tstr = return_str;
		tstr2 = parse_str('F=',tstr);
		if(  tstr2 != cur_file  ) {
			call open_file2;
		}
		help_select( caps(parse_str('C=',tstr )));
		help_current_offset = parse_int('O=', tstr );
		help_current_link = parse_int('L=', tstr );
		need_new_bar = TRUE;
	}
	ret;

exit:
	call help_push_pos;
	if(  help_open  ) {
		help_done;
	}


	while(  box_count > tbc  ) {
		kill_box;
	}
	pop_labels;
	gotoxy( tx, ty );
	mou_repeat = trepeat;
	fkey_row = tfkey_row;
	status_row = tstatus_row;
	refresh = trefresh;
	Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') - 1);
	RM('USERIN^CheckEvents /M=3/G=' + event_str + '/#=' + str(event_count));
total_exit:
}

macro HELP_LIST_MARKS trans2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  HELP_LIST_MARKS

Description: Brings up a list of currently defined help book marks.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	RM('USERIN^DB /GLO=@BOOKMARK/F=MECONFIG.DB/DPT=HELPMARK.DB/LO=1/LT=HELP BOOKMARKS/2TOP=1/NI=1/NE=1/NC=1');
	if(  return_int  ) {
		return_str = global_str('@BOOKMARK');
		if(  NOT( parse_int('/FROMHELP=', mparm_str) )  ) {
			set_global_str('@BOOKMARK','');
			RM('MEHELP /F=' + parse_str('F=', return_str) +
					'/LK=' + parse_str('C=', return_str) +
					'/TO=' + parse_str('O=', return_str ));
		}
	}
} /* HELP_LIST_MARKS */

macro ScreenMrk trans2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  ScreenMrk

Description:	Allows any part of the screen to be marked, and have
	that section of the screen copied to a buffer.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  need_hi, oldx,oldy,x1, y1, xi, yi, xl, jx, yl, mouse_down, tmou_repeat ;
	int  temp_fkey_row, temp_status_row, tr ;
	int  mm = parse_int('/MM=',mparm_str) ;
	oldx = wherex;
	oldy = wherey;
	tr = refresh;
	refresh = false;
	temp_fkey_row = fkey_row;
	fkey_row = 0;
/* Toggle insert mode to change cursor size and draw attention to it */
	insert_Mode = Not(Insert_Mode);
	gotoxy(Screen_Width / 2,Screen_Length / 2);
	draw_char( 32, 1, Temp_fkey_row, fkey_color, screen_width );
	if(  not(mm)  ) {
		write( 'MARKING: Use |24|25 |27|26 <Home> <End> to move cursor.  <Enter> to begin marking.', 1, Temp_fkey_row, 0, Fkey_color );
	}
	push_labels;
	temp_status_row = status_row;
	status_row = 0;
	need_hi = false;
	mouse_down = FALSE;
	tmou_repeat = mou_repeat;
	mou_repeat = FALSE;
	if(  mm  ) {
		mouse_down = TRUE;
		gotoxy( mou_last_x, mou_last_y );
		goto start2;
	}
loop1:
	read_key;
	if(  key1 == 0  ) {
		if(  key2 == 250  ) {
			mouse_down = TRUE;
			gotoxy( mou_last_x, mou_last_y );
			goto start2;
		} else {
			call arrow_keys;
		}
	} else if(  key1 == 13  ) {
		write( 'MARKING: Use |24|25 |27|26 <Home> <End> to move cursor.  <Enter> to end marking.  ', 1, Temp_fkey_row, 0, Fkey_color );
		goto start2;
	} else if(  key1 == 27  ) {
		goto exit;
	}
	goto loop1;

start2:
	x1 = wherex;
	y1 = wherey;
	save_box( 1, 1, screen_width, screen_length );
	call draw_highlight;
loop2:
	if(  check_key  ) {
		if(  key1 == 0  ) {
			if(  key2 == 250  ) {
				gotoxy( mou_last_x, mou_last_y );
				call draw_highlight;
				goto accept;
			} else {
				CALL arrow_keys;
			}
		} else if(  key1 == 13  ) {
			goto accept;
		} else if(  key1 == 27  ) {
			goto exit2;
		}
	} else if(  mouse_down  ) {
		Mou_Check_Status;
		if (Mou_Last_Status & 1) {
			if ((mou_last_x != wherex) || (mou_last_y != wherey)) {
				gotoxy( mou_last_x, mou_last_y);
				need_hi = TRUE;
			} else {
			}
		} else {
			goto accept;
		}
	}
	if (need_hi) {
		call DRAW_HIGHLIGHT;
	}
	goto loop2;

accept:
	call DRAW_HIGHLIGHT;
	RM('USERIN^XMENU /L=Screen Paste/T=1/B=1/X' + str(xi) +
						'/Y=' + str(yi) +
			'/M=Copy text to buffer, erasing old contents()Append text to end of buffer()send text to the Printer()');
	if(  (return_int < 1)  ) {
		goto exit2;
	} else {
		RM('ScreenCut /X1=' + str( xi ) + '/X2=' + str( xi + xl - 1) +
						'/Y1=' + str(yi) + '/Y2=' + str(yl) + '/M=' + str( return_int - 1 ) );
	}

EXIT2:
	kill_box;
	GOTO EXIT;

ARROW_KEYS:
	if (key2 == 72) { /* up */
		if(  wherey > 1  ) {
			gotoxy(wherex, wherey - 1);
			need_hi = TRUE;
		}
	} else if (key2 == 80) { /* down */
		if(  wherey < screen_length  ) {
			gotoxy(wherex, wherey + 1);
			need_hi = TRUE;
		}
	} else if (key2 == 77) { /* right */
		if(  wherex < screen_width  ) {
			gotoxy(wherex + 1, wherey);
			need_hi = TRUE;
		}
	} else if (key2 == 75) { /* left */
		if (wherex > 1) {
			gotoxy(wherex - 1, wherey);
			need_hi = TRUE;
		}
	} else if (key2 == 71) { /* home */
			gotoxy(1, wherey);
			need_hi = TRUE;
	} else if (key2 == 79) { /* end */
			gotoxy(screen_width, wherey);
			need_hi = TRUE;
	}
	RET;

DRAW_HIGHLIGHT:
	SET_VIRTUAL_DISPLAY;
	need_hi = false;
	RESTORE_BOX;
	if(  x1 > wherex  ) {
		xi = wherex;
		xl = x1 - wherex + 1;
	} else {
		xi = x1;
		xl = wherex - x1 + 1;
	}
	if(  y1 > wherey  ) {
		yi = wherey;
		yl = y1;
	} else {
		yi = y1;
		yl = wherey;
	}
	jx = yi;
	while(  jx <= yl  ) {
		draw_attr( xi, jx, m_h_color, xl );
		++jx;
	}
	UPDATE_VIRTUAL_DISPLAY;
	RESET_VIRTUAL_DISPLAY;
	RET;


EXIT:
	status_row = temp_status_row;
	fkey_row = temp_fkey_row;
	pop_labels;
	gotoxy(oldx, oldy);
	mou_repeat = tmou_repeat;
	insert_Mode = Not(Insert_Mode);
	refresh = tr;
}

macro ScreenCut trans2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:	ScreenCut

Description:

Parameters:
							/X1= the upper left hand column
							/Y1= the upper left hand row
							/X2= the lower right hand column
							/Y2= the lower right hand row
							/M=nn  mode
										0 = Copy block to buffer, overwriting old contents
										1 = Append block to buffer
										2 = Send block to printer device.  In this mode, all
												characters whose ASCII code < 32 are replaced with
												spaces.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int tr, Active_Page, ox,x1,y1,x2,y2, old_win, tu,
					cut_mode = parse_int('/M=', mparm_str);
	str TStr, buf_name;

	tr = refresh;
	refresh = FALSE;
  mou_remove_ptr;
	tu = undo_stat;
	undo_stat = FALSE;
	old_win = window_id;
  Buf_name = 'SCREEN.TMP';

  switch_window(window_count);
  create_window;
  if(  error_level != 0  ) {
    goto exit;
  }
  window_attr = $81;
  file_name = buf_name;

	x1 = parse_int('/X1=', mparm_str);
	y1 = parse_int('/Y1=', mparm_str);
	x2 = parse_int('/X2=', mparm_str);
	y2 = parse_int('/Y2=', mparm_str);

	R_AX = $0F00;
	Intr($10);
	Active_Page = R_BX;
	ox = x1;
	while (y1 <= y2) {
		tstr = '';
/* Get the screen a line at a time */
		x1 = ox;
		while(  (x1 <= x2)  ) {
/* Get the character at the cursor */
			GotoXY(x1,y1);
			R_AX = $0800;
			R_BX = Active_Page;
			Intr($10);
			TStr = TStr + Char(R_AX & $FF);
			++x1;
		}
		++y1;

		put_line( tstr );
		down;
	}
	tof;
	block_begin;
	eof;
	block_end;
	if(  (Cut_Mode == 2)  ) {
/* Filter out all control characters */
		Tof;
		while (Search_Fwd('[|0-|31]',0)) {
			Replace(' ');
		}
/* Send to printer */
		ox = Print_Margin;
		Print_Margin = 1;
		RM('MEUTIL3^PRINT_FILE_BLOCK');
		Print_margin = ox;
	}
  else
  {
    if(cut_mode == 1)
    {
      rm("CUT /A");
    }
    else
    {
      rm("CUT");
    }
  }
  Delete_Window;
exit:
  mou_draw_ptr;
	switch_win_id( old_win );
	undo_stat = TU;
	refresh = TR;
}

macro Get_Context trans2 {
/*******************************************************************************
															 MULTI-EDIT MACRO
															 03-05-90 10:11am

	Macro   : Get_Context
	Function: Return the word under the cursor defined by the passed Regular
						Expresson or the word marked by a column or stream block.  The
						marked word will only be used if the cursor is on the same line
						as the marked word.
	Syntax  : RM('Get_Context');
	Entry   : Return_Str = Regular Expression defining context word.
							Default Regular Expression = '[A-Za-z0-9._]'.
	Exit    : Return_Str = Context word under the cursor or marked block.

							 (C) Copyright 1991 by American Cybernetics, Inc.
***********************************************************************(ldh)***/

	str tstr[100], tstr2[100];
	int org_refresh, org_reg, col;
	int tpb = persistent_blocks;
	persistent_blocks = TRUE;

	/* Set up our context */
	org_refresh = REFRESH;
	REFRESH = FALSE;                       /* No flashing of windows */
	org_reg = Reg_Exp_Stat;
	Reg_Exp_Stat = TRUE;                   /* Regular expression search */

	Mark_Pos;                               /* Save current column position */

	tstr = Return_Str;                     /* Get Word Definition */
	if(  tstr == ''  ) {
		tstr = '[A-Za-z0-9._]';
	}
	tstr2 = Str_Ins('~', tstr, 2);

	if(  Marking  ) {
		Block_End;
	}
	if(  (Block_Stat > 1) & (Block_Line1 == C_Line) & (Block_Line2 == C_Line)  ) {
		Goto_Col(Block_Col1);
		col = Block_Col2 + 1;
	} else {
		Search_Bwd(tstr, 1);
		if(  Search_Bwd(tstr2, 1) == 0  ) {
			Goto_Col(1);
		}
		Search_Fwd(tstr, 1);
		Mark_Pos;
		if(  Search_Fwd(tstr2, 1) == 0  ) {
			EOL;
		}
		col = C_Col;
		Goto_Mark;
	}

	/* Grab the characters that make up this word */
	Return_Str = '';
	while(  (C_Col < col)  ) {
		Return_Str = Return_Str + Cur_Char;
		right;
	}

	/* Restore original cursor position */
	Goto_Mark;

	REFRESH = org_refresh;
	Reg_Exp_Stat = org_reg;
	persistent_blocks = tpb;
}  /* Get_Context */
