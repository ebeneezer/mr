macro_file MESYS;


#INCLUDE WINDOW.SH


/*******************************************************************************
														MULTI_EDIT MACRO FILE

MESYS is loaded automatically by Multi-Edit upon startup.  Multi-Edit will
abort if it cannot find the MESYS macro file.

MESYS contains the following macros:

UNDO            Executes an UNDO or REDO
SETSCRN         Sets up the screen based on the selected style.
XZOOM           Toggles between screen styles.
USER_GO         Executes an item as defined in a user menu
EDIT_USER_MENU  Brings up a scrolling list of a user definable menu.
EDIT_MENU_PARMS Allows editing of user definable menu parameters.  Used by
									EDIT_USER_MENU.
BUILD_USER_MENU Builds a menu from menu .DB data
MENU            Displays a user definable menu
MEMENUS         The main menu.
INSTGL          Toggles insert/overwrite mode.
BSWORD          Deletes the word before the cursor.
DELWORD         Deletes the word under the cursor.
DELEOL          Delete to end of line.
DEL_TO_HOME     Deletes to the home position of the line
DEL             Processes the <DEL> key in a CUA compliant manner.
HOME            Implements the multi-stroke start of line/window/file function.
END             Implements the multi-stroke end of line/window/file function.
SHIFT_CURSOR    Handles SAA/CUA complient shifted cursor key use
CR              Implements carriage return with indenting styles.
TAB             Processes <TAB> and <ShiftTAB> in a CUA compiant manner
DATETIME        Stamps the date and time into a file.
GOTOLINE        Prompts for a line number to go to.
TOPBLOCK        Cursor to top of block.
ENDBLOCK        Cursor to end of block.
TOPWIN          Cursor to top of window.
BOTWIN          Cursor to bottom of window.
SCROLLUP        Scroll the window up one line.
SCROLLDN        Scroll the window down one line.
PAGEBRK         Insert a line with a page break.
FIND_EXT        Retrieves the extension specific setup from EXT.DB.
SETCONFIG       Finds a database in a .DB file
GET_DB_RECORD   Finds a DB record in a .DB file
MAKEUSERPATH    Searches for a user specific file name.  Creates it if neccesary.
XLATECMDLINE    Translates special "aliases", like <FILE>, <EXT>, etc.
SETTABS         Creates a temporary format line from the tab spacing parameter.
EXTSETUP        Setup a file/window format based on its extension.
PATHSEARCH      Searches for a filename in a given path
SETFILENAME     Adds in the default directory for an extension.
CREATEWINDOW    Creates a new window at the end of the window list.
LDFILES         Load files into windows.  Allows wildcards.
AUTOSAVE        Implements the automatic save feature.
AUTOSAVETIMER   Manages autosave timer events.
SETAUTOSAVE     Initializes timer events for AUTOSAVE
SETWINDOWNAMES  Creates sequential window letters for all editable windows.
KEYMAC_LOAD     Loads, saves, erases keystroke macros from KEYMAC.DB
PARMS1          Process command line switches.
MOUTYPE         Sets the mouse cursor visually.
PARMLOAD        Load files from the command line.
SPACE           Inserts a space character at the current cursor position.

							 (C) Copyright 1991 by American Cybernetics, Inc.
********************************************************************************/


macro UNDO TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: UNDO

Description:  Performs an UNDO or REDO

Parameters:   /R        causes a REDO instead of an UNDO

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	if(  xpos('/R', caps(mparm_str),1)  ) {
		redo;
	} else {
		undo;
	}
}

void SETSCRN(int set_icon_rows = parse_int('/SIROW=', mparm_str),
              int t_icon_rows = parse_int('/IROWS=', mparm_str),
              int ignore_icons = parse_int('/ICON=', mparm_str ),
              str win_parms = parse_str('/P=', mparm_str),
              int set_min_window_row = parse_int('/M=',mparm_str),
              int t_min_window_row = parse_int('/MWR=', mparm_str),
              int delete_flag = parse_int("/DELETE=",mparm_str)) {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SETSCRN

Description:  Sets up the screen based on the screen style set in the global
			int CUR_SCRN.  This is setup in the installation and setup menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

/*
set_global_int("!@#TR",global_int("!@#TR") + 1);
set_global_str("!@#TR" + str(global_int("!@#TR")),
"/SIROW=" + str(set_icon_rows) +
'/IROWS=' + str(t_icon_rows) +
'/ICON=' + str(ignore_icons) +
'/P=' + win_parms +
'/M=' + str(set_min_window_row) +
'/MWR=' + str(t_min_window_row) +
"/DELETE=" + str(delete_flag));
*/
    int  jx, temp_win, old_row, old_min_col, old_max_col,
         old_min_row = min_window_row, old_max_row = max_window_row,
         nmx,mb, fx,sx,mx, old_max, old_max2,
        old_min, ny1, ny2, tr, nx1, nx2, icon_rows, old_icon_rows,
        lower_sub;

    int  bb, /*ignore_icons,*/ t_int, old_width, old_height ;
    str  /*win_parms[100],*/ tstr[132];
		char  tchar ;

		tr = refresh;
		refresh = false;

		temp_win = cur_window;
		SET_GLOBAL_INT('@MENUROW@', 0);

    old_icon_rows = icon_rows = Global_Int( 'ICON_ROWS');
		old_max2 = max_window_row;
		old_max = max_window_row - icon_rows;
/*
		if( parse_int('/SIROW=', mparm_str)) {
			icon_rows = parse_int('/IROWS=', mparm_str);
			Set_Global_Int( 'ICON_ROWS', icon_rows);
		}
*/
    if(set_icon_rows) {
      icon_rows = t_icon_rows;
/*
      if (get_param_type(1) == 2) {
        icon_rows = get_param_int(1);
      } else {
        icon_rows = parse_int('/IROWS=', mparm_str);
      }
 */
			Set_Global_Int( 'ICON_ROWS', icon_rows);
		}

		old_min = min_window_row;
		old_max_col = max_window_col;
		old_min_col = min_window_col;

//    ignore_icons = parse_int('/ICON=', mparm_str );
/*
		win_parms = parse_str('/P=', mparm_str);
		if(  parse_int('/M=',mparm_str) == 1  ) {
			min_window_row = parse_int('/MWR=', mparm_str);
			goto set_windows_only;
		}
*/
    if (set_min_window_row == 1) {
      min_window_row = t_min_window_row;
/*
      if (get_param_type(2) == 2) {
        min_window_row = get_param_int(2);
      } else {
        min_window_row = parse_int('/MWR=', mparm_str);
      }
*/
			goto set_windows_only;
		}
		tchar = Char(Global_Int('CUR_SCRN') + 64);
		if(  (tchar == 'A') | (tchar == 'B')  ) {
			win_parms = global_str('@SCREEN_SET' + tchar);

			fx = parse_int( '/F=',win_parms );
			sx = parse_int( '/S=',win_parms );
			mx = parse_int( '/M=',win_parms );
			mb = parse_int( '/MB=', win_parms );

			min_window_col = parse_int( '/LB=', win_parms );
			max_window_col = (screen_width + 1) - parse_int( '/RB=', win_parms );

			set_global_int('!NEW_MENU_STYLE', 0);
			status_macro('');

			tstr = 'Multi-Edit V' + version;
			pad_str( tstr, screen_width - 14, ' ' );
			status_str =  tstr + 'L:      C:    ';


			mode_col = 0;
			insert_col = screen_width - 18;
			mem_col = 0;

			caps_col = screen_width - 15;
			record_col = screen_width - 14;

			line_col = screen_width - 11;
			col_col = screen_width - 3;

			status_row = 1;
			message_col = 1;
			message_length = screen_width - 23;
			working_col = screen_width - 22;
			menu_bar_str = '';
			RM('MENU /MN=MAIN/BUILD=1');
			tstr = menu_bar_str;
			time_col = screen_width - 14;
			pad_str( tstr, time_col - 1, ' ');
			menu_bar_str = tstr;


			if(  FX == 0  ) {
				Fkey_Row = 0;
			} else {
				Fkey_Row = Screen_Length;
			}

			if(  SX == 0  ) {
				Status_Row = 0;
				message_row = 0;
			} else {
				Status_Row = 1;
				message_row = 1;
			}


			if(  mb != 0  ) {
				menu_bar_row = status_row + 1;
				menu_bar_col = 1;
			} else {
				menu_bar_row = 0;
			}
	set_windows_only:
			min_window_row = sx + mb + 1;
			time_row = menu_bar_row;
			bb = parse_int('/BB=', win_parms);
			set_global_int( '~WIN_BOTTOM_BORDER', bb );
			nmx = Screen_Length;
			max_window_row = nmx;
      if( icon_rows ) {
				nmx = nmx - icon_rows - (fx != 0);
				max_window_row = max_window_row - (fx != 0);
      } else {
				max_window_row = max_window_row - bb + (fx == 0);
				nmx = max_window_row;
			}
			if( old_max > nmx )
				lower_sub = 0 - (old_max - nmx);
			else
				lower_sub = nmx - old_max;

			if( old_max2 > max_window_row )
					old_max2 = 0 - (old_max2 - max_window_row);
			else
					old_max2 = max_window_row - old_max2;

			jx = xpos('/MACRO=', win_parms, 1 );
			if(  jx != 0  ) {
				tstr = shorten_str(copy( win_parms, jx + 7, 40 ));
				if(  tstr != ''  ) {
					RM(tstr);
				}
			}
set_windows:
//      jx = 0;
//      while(  jx < window_count  ) {
      for (jx = 0; jx < window_count; ++jx) {

        nx1 = win_x1;
        nx2 = win_x2;
        ny1 = win_y1;
        ny2 = win_y2;
        old_width = nx2 - nx1;
        old_height = ny2 - ny1;

				if(  !(ignore_icons && ((window_attr & $40) != 0))  ) {
					if(  nx1 == old_min_col  ) {
						nx1 = min_window_col;
					}
					if(  nx2 == old_max_col  ) {
						nx2 = max_window_col;
					}
					if(  (window_attr & $40)  ) {
						ny1 = win_y1 + old_max2;
						if(  ny1 > (max_window_Row)  ) {
							ny1 = max_window_row;
						}
						if( ny1 < min_window_row) {
							ny1 = min_window_row;
						}
						ny2 = ny1;
					} else {

            if (ny1 == old_min_row) {  // only move this if it is on the screen border
              ny1 = min_window_row + (win_y1 - old_min);
              if (ny1 > (max_window_Row - 1 - icon_rows)) {
                ny1 = max_window_row - 1 - icon_rows;
              }
            }
/*
if (ny2 == (old_max_row - old_icon_rows)) {
} else {

BEEP;
make_message(file_name + " SIR=" + str(set_icon_rows) +
    " II=" + str(ignore_icons) +
    " WA=" + str(window_attr & 0x40));
read_key;

}
*/
            if (ny2 >= (old_max_row - old_icon_rows)) {
              ny2 = win_y2 + lower_sub;
              if ( ny2 > nmx )
                ny2 = nmx;
            }
          }

					if( window_attr & $40 ) {
						if(nx1 == 0) {
							++nx1;
							++nx2;
						}
					}

// check for out of bounds coordinates and compensate if necessary
          if ((ny2 - ny1) < 0) {
            // attempt to make the window the same size as before
            ny1 = ny2 - old_height;
          } else if ((nx2 - nx1) < 1) {
            nx1 = nx2 - old_width;
          }
          if (delete_flag) {
            Size_window(nx1,ny1,nx2,ny2);
          } else {

/* running this special mode of MOD_WIN instead of size_window() insures that
   split windows are taken into account */
/*
beep;
make_message("before size_window: nmx=" + str(nmx) + " MWR=" +
    str(max_window_row) + " Y2=" + str(ny2));
read_key;
*/
            return_str = "/X1=" + str(nx1) +
                         "/Y1=" + str(ny1) +
                         "/X2=" + str(nx2) +
                         "/Y2=" + str(ny2);
            MOD_WIN(0,0,1);
//            RM("MOD_WIN /NOPROMPT=1");
          }
				}
//        ++jx;
				Switch_Window(cur_window + 1);
			}


//      if (Global_Int('!AutoArrangErrSrc')) {
      if (Global_Int('!AutoArrangErrSrc') &&
          ((old_max_row - old_icon_rows) != (max_window_row - icon_rows))) {
        jx = window_id;

//        if( switch_win_id(Global_Int('~MEERR_SPLIT_ID')) ) {
//          ny1 = win_y2 + global_int('Split_Edge_Mode');
//          if( switch_win_id(Global_Int('~MEERR_ID')) ) {
//            size_window( win_x1, ny1, win_x2, ny1 + 3 );
//          }
//        }

        if (switch_win_id(Global_Int('~MEERR_ID'))
            && (!(window_attr & $40))) {

// check for out of range values of ~MEERR_HEIGHT
            if (Global_int("~MEERR_HEIGHT") < 2) {
              Set_Global_Int("~MEERR_HEIGHT", 2);
            }
            if (Global_int("~MEERR_HEIGHT") > (max_window_row - min_window_row - 4)) {
              Set_Global_Int("~MEERR_HEIGHT", max_window_row - min_window_row - 4);
            }

            ny2 = win_y2 - Global_int("~MEERR_HEIGHT") - 2;

            size_window( win_x1, ny2 + global_int('Split_Edge_Mode'),
                win_x2, win_y2);
          if (switch_win_id(Global_Int('~MEERR_SPLIT_ID'))) {
              if (!(window_attr & $40)) {
                size_window( win_x1, win_y1, win_x2, ny2);
              }
          }
        }
				switch_win_id( jx );
			}
		}

    if( !global_int("@NO_MOUSE_SET"))
			rm("INIT_MOUSE");
		new_screen;
		refresh = tr;
    RETURN();
}

macro init_mouse
{
	str tstr[128];
	if( global_int('~INIT_MOUSE' ) || parse_int("/INIT=", mparm_str) ) {
		set_global_int('~INIT_MOUSE', 0 );
		mouse = TRUE;
	}
	if(  mou_installed  ) {
		int tw, jx;
	/* Setup mouse sensitivity */
		tstr = global_str('@MOUSEPARMS');
		Mou_Set_Limits( 1, 1, Screen_Width, Screen_Length );
		jx = parse_int('/S=',tstr);
		if(  (jx != 0)  ) {
			r_bx = jx;
			r_cx = jx;
			r_dx = jx;
			r_ax = 26;
			intr($33);
		}
		jx = parse_int('/CT=', tstr);
		tw = parse_int('/MC=', tstr);
		if(  jx == 0  ) {
			tw = 0;
		}
		RM('MOUTYPE /C=' + str(tw) + '/M=' + parse_str('/CA=', tstr) + '/A=' + parse_str('/MA=', tstr) );
	}
}

macro XZOOM TRANS {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: XZOOM

Description:  toggles between screen styles.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int jx;
	jx = Global_Int('CUR_SCRN');
	if(  jx == 1  ) {
		jx = 2;
	} else {
		jx = 1;
	}
	set_global_int('Cur_Scrn',jx);
//  RM('SETSCRN');
  SETSCRN(0,0,0,"",0,0,0);
	new_screen;
}

macro user_go trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: USER_GO

Description:  Executes an item as defined in a user menu

Parameters:  None.  Gets information from Global_Str('@USER@')

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int dm, ct, cmd, jx, old_sr, smem, result = 1000, Wait = 0 ;
	str hf[40],
			cur_dir[128],
			cmdline
			;

	refresh = false;
  return_str = parse_str('CMD=', global_str('@USER@'));
	RM( 'XlateCmdLine /F=' + file_name);
  cmdline = return_str;
	ct = parse_int( 'TYPE=', global_str( '@USER@'));
	Wait = parse_int( 'WAIT=', global_str( '@USER@'));
	switch(ct) {
		case 0 :
		case 1 :
					if ( '' == MParm_Str )
						RM(cmdline);
					else
						RM(cmdline+' '+MParm_Str);
					break;
		case 2 :
					old_sr = status_row;
					status_row = 0;
					if(  parse_int('|127SAVEFILES=', global_str('@USER@'))  ) {
						rm('AUTOSAVE');
					}
					smem = (parse_int('SWAP_MEM=', global_str('@USER@') ) * 1024) / 16;
					cmd = parse_int('USECMD=', global_str('@USER@'));
					dm = parse_int('CS=',  global_str('@USER@'));

					if(  parse_str('DIR=', global_str('@USER@')) != ''  ) {
						cur_dir = fexpand('');

							/* 09-17-90 12:23pm  This fixes the problem of an ending backslash */
						if(  (svl(cur_dir) > 3) & (COPY( cur_dir, svl(cur_dir), 1) == '\')  ) {
							cur_dir = str_del( cur_dir, svl(cur_dir), 1 );
						}
							/* end of fix 09-17-90 12:26pm */

						change_dir(parse_str('DIR=', global_str('@USER@')));
					}

					return_str = cmdline;
					RM( 'MEUTIL1^EXEC /MEM=' + str(smem) +
								'/CMD=' + str(cmd) + '/SCREEN=' + str(dm) + '/WAIT=' + STR(WAIT) );
					if(  parse_str('DIR=', global_str('@USER@')) != ''  ) {
						change_dir(cur_dir);
					}
					status_row = old_sr;
					New_Screen;
					break;
		case 3 :
					jx = XPOS('^', cmdline, 1);
					if(  jx == 0  ) {
						jx = length(cmdline);
					}
					hf = copy(cmdline, 1, jx - 1);
					cmdline = copy(cmdline, jx + 1, 254 );
					update_status_line;
					RM('MEHELP /F=' + hf + '/LK=' + cmdline );
					break;
		case 4 :
			if(  switch_file( caps(cmdline) )  ) {
				make_message( truncate_path(cmdline) + ' already loaded.');
			} else {
				return_str = cmdline;
				RM('LDFILES /LC=1');
			}
			break;
		case 5 :
			rm('EDIT_USER_MENU /MN=' + cmdline + mparm_str );
			if( return_int < 1)
					result = 0;
			break;
	}
	return_int = result;
}

macro EDIT_USER_MENU TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: EDIT_USER_MENU

Description: Brings up a scrolling list of a user definable menu.

Parameters:   /MN=str   menu name
							/ED=1     allow editing of sub-menus
							/DBF=str  filename to find menu

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str menu_name[20] = shorten_str(parse_str('/MN=', mparm_str));
	str dbf_name[80] = shorten_str(parse_str('/DBF=', mparm_str));
	int result, tx;

	refresh = false;
	if( menu_name == '') {
			menu_name = 'MAIN';
	}
	tx = xpos( '^', menu_name, 1 );
	if(tx != 0) {
		dbf_name = copy( menu_name, 1, tx - 1 );
		menu_name = copy( menu_name, tx + 1, 254 );
	}
	if (dbf_name == '')
			dbf_name = me_path + 'RESOURCE.DB';

	do {
		set_global_str('USERBUTTON1', '/T=Edit menu parameters/QK=1/W=20/R=9');
		RM('USERIN^DB /EBC=1/EBN=USERBUTTON/MOVE=1/ABT=Go/NDF=1/F=' + dbf_name +
						'/NOALPHA=1/HF=MECONFIG.DB/HPT=USERMENU.HDR/DPT=' +
						menu_name + '.MNU/LT=Edit menu: ' + menu_name +
						'/LO=2/H=USER/GLO=@USER@');
		set_global_str('USERBUTTON1', '');
		result = return_int;
		if(  return_int == 1  ) {
			rm('user_go /DBF=' + dbf_name + '/BUILD=' + parse_str('/BUILD=', mparm_str));
			return_int = (return_int == 0);
		} else if(return_int == 9) {
			rm('edit_menu_parms /DB=' + dbf_name + '/MN=' + menu_name  + '/BUILD=' + parse_str('/BUILD=', mparm_str) +
							'/BP=' + str( parse_int('/BP=', mparm_str)));
			return_int = TRUE;
		} else {
			return_int = FALSE;
		};
	} while( return_int );
	if(parse_int('/BUILD=', mparm_str))
		RM('build_user_menu /MN=' + menu_name  + '/DBF=' + dbf_name + mparm_str);
	return_int = result;
}

macro edit_menu_parms trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: EDIT_MENU_PARMS

Description: Allows editing of user definable menu parameters.  Used by
						 EDIT_USER_MENU.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
		int menu = menu_create;
		int jx,
				owin = window_id;

		RM('SETCONFIG /DB=' + parse_str('/DB=', mparm_str) +
					'/C=1/T=' + parse_str('/MN=', mparm_str) + '.MNU');
		if(return_int) {
			return_int = menu;
			menu_set_item( return_int, 1, "Title:", parse_str('/T=', get_line ), "/L=1/C=1/W=20/ML=80/QK=1", 0, 0, 0);
			menu_set_item( return_int, 2, "Help index: ", parse_str('/H=', get_line), "/L=2/C=1/W=20/ML=40/QK=1", 0, 0, 0);
			menu_set_item( return_int, 3, "Setup macro:", "", "/L=3/C=1/W=20/ML=80/QK=1", 0, 0, 0);
			menu_set_item( return_int, 4, "Horizontal    ", "", "/L=5/C=1/QK=1", 13, parse_int('/HZ=', get_line), 0);

			if(parse_int("/BP=", mparm_str)) {
				menu_set_item( return_int, 5, " [X] Allow editing ", "", "/L=6/C=1/ATTR=" + str(m_k_color), 10, 1, 0);
				menu_set_item( return_int, 6, " [ ] Transient     ", "", "/L=7/C=1/ATTR=" + str(m_k_color), 10, 0, 0);
			}
			else {
				menu_set_item( return_int, 5, "Allow editing ", "", "/L=6/C=1/QK=1", 13, parse_int('/ED=', get_line), 0);
				menu_set_item( return_int, 6, "Transient     ", "", "/L=7/C=1/QK=2", 13, parse_int('/TRANS=', get_line), 0);
			}
			jx = xpos( '/M=', get_line, 1 );
			if(jx != 0) {
				menu_set_str(menu,3,2, copy(get_line, jx + 3, 254) );
			}
			return_int = menu;
			rm('USERIN^DATA_IN /HN=1/#=6/T=MENU PARAMETERS' );
			if(return_int) {
				goto_col(1);
				forward_till(' ');
				put_line( copy(get_line,1, c_col - 1) + ' /HZ=' + str( menu_item_int( menu, 4, 2 ))
									+ '/ED=' + str( menu_item_int( menu, 5, 2 ) ) +
									'/H=' + menu_item_str(menu,2,2) +
									'/TRANS=' + str(menu_item_int(menu,6,2)) +
									'/T=' + menu_item_str(menu,1,2) +
									'/M=' + menu_item_str(menu,3,2)
									);
			}
		}
		menu_delete( menu );
		switch_win_id( owin );
}

macro build_user_menu trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: BUILD_USER_MENU

Description:  Builds a menu from menu .DB data

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str menu_name[20] = shorten_str(parse_str('/MN=', mparm_str)),
			dbf_name[80] = shorten_str(parse_str('/DBF=', mparm_str)),
			menu_parms[80] = '',
			tstr[80], tstr2[20],
			xstr
			;


	int omenu_count = parse_int('/#=', global_str( '~' + menu_name + '_MENU_P') ),
			menu_count = 0,
			owin = window_id,
			do_edit,
			top_menu,
			t,
			jx, jy, s,
			menu = global_int('~' + menu_name + '_MENU')
			;

	if (dbf_name == '')
			dbf_name = me_path + 'MENUS.DB';
	if (menu != 0)
			menu_delete( menu );
	menu = menu_create;
	set_global_int('~' + menu_name + '_MENU', menu );
	refresh = FALSE;
	RM('SETCONFIG /DB=' + dbf_name + '/C=1/T=' + menu_name + '.MNU');
	if( return_int ) {
		goto_col(1);
		forward_till(' ');
		right;
		menu_parms = get_word('');
		top_menu = parse_int('/HZ=', menu_parms);
		do_edit = (parse_int('/ED=', menu_parms) || parse_int('/ED=', mparm_str));
		reg_exp_stat = TRUE;
		down;
		goto_col(1);
		while( (cur_char != '|127') && !at_eof && (cur_char != '|12')) {
			down;
		}

	loop:
		xstr = get_line;
		if( svl(xstr) != 0) {
			if( str_char(xstr,1) != '|12') {
				++menu_count;
				var_parse_int('|127TYPE=', xstr, t);
				if (t == 6 )
						menu_set_item( menu, menu_count, '','','',1,0,0 );
				else
						menu_set_item( menu, menu_count, parse_str('|127NAME=', xstr),'', '',0,0,0 );
				var_remove_str( '|127NAME=', xstr );

				return_str = parse_str('|127HNDX=', xstr );
				var_remove_str( '|127HNDX=', xstr );
				if(t == 5) {
						int tx;

						if(!top_menu)
								menu_set_str( menu, menu_count, 1, menu_item_str(menu,menu_count,1) + '  |16' );
						var_parse_str('|127CMD=', xstr, tstr);
						tx = xpos( '^', tstr, 1 );
						if(tx != 0) {
							tstr2 = copy( tstr, 1, tx - 1 );
							tstr = copy( tstr, tx + 1, 254 );
						}
						else
								tstr2 = dbf_name;
						menu_set_str( menu, menu_count, 3, '/S=5/H=' + return_str + '/ ' + xstr + '/M=MENU /MN=' + tstr
								+ '/DBF=' + tstr2 );
				} else {
						menu_set_str( menu, menu_count, 3, '/S=2/H=' + return_str + '/ ' + xstr );
				}
				down;
				goto loop;
			}
		}
	}
	set_global_str('~' + menu_name + '_MENU_P', '/#=' + str( menu_count ) +
				menu_parms );
	switch_win_id( owin );
}

macro MENU no_break {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: MENU

Description:  Displays a user definable menu

Paramters:  /MN=str   menu name
						/ED=1     edit allow editing of sub-menus
						/DBF=str  file name that contains the menu
						/BUILD=1  Build top menu bar, and then exit

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int tx;
	int do_edit = parse_int('/ED=', mparm_str );
	int top_menu, db_menu = parse_int('/MX=', mparm_str);
	str help_ndx[40],
			dbf_name[80] = shorten_str(parse_str('/DBF=', mparm_str));
	str menu_name[80] = shorten_str(parse_str('/MN=', mparm_str)),
			lstr[80];
	int  menu,
				menu_count,
				full_menu_count,
				jx,
				edit_num
			;


	if( menu_name == '') {
			menu_name = 'MAIN';
			do_edit = TRUE;
	}
	tx = xpos( '^', menu_name, 1 );
	if(tx != 0) {
		dbf_name = copy( menu_name, 1, tx - 1 );
		menu_name = copy( menu_name, tx + 1, 254 );
	}
	if (get_extension(dbf_name) != '')
			db_menu = true;

again:
	if(db_menu) {
		menu = global_int('~' + menu_name + '_MENU');
		if( menu == 0) {
			RM('build_user_menu /MN=' + menu_name + '/DBF=' + dbf_name);
			menu = global_int('~' + menu_name + '_MENU');
		}
		if (menu == 0) {
			beep;
			goto exit;
		}
		return_str = global_str('~' + menu_name + '_MENU_P');
	} else {
		if (dbf_name == '') {
			dbf_name = 'RESOURCE';
		}
		menu = menu_create;
		return_int = menu;
		rm(dbf_name + '^_MN_' + menu_name);
	}
	menu_count = parse_int('/#=', return_str);
	top_menu = parse_int('/HZ=', return_str);
	do_edit = (do_edit || parse_int('/ED=', return_str) && (db_menu != 0));
	help_ndx = parse_str('/H=', return_str );
	lstr = parse_str('/T=', return_str);
	if( lstr == '')
			lstr = menu_name;
	// execute the setup macro for this menu if one has been specified
	tx = xpos('/M=', return_str, 1);
	if(tx > 0) {
		return_str = shorten_str(copy(return_str, tx + 3, 254 ));
		if(return_str != '')
			rm(return_str);
	}

	if(parse_int('/BUILD=', mparm_str)) {
		dbf_name = '';
		for( jx = 1; jx <= menu_count; jx++ ) {
			dbf_name += menu_item_str( menu, jx, 1 ) + ' ';
		}
		pad_str( dbf_name, 65, ' ');
		menu_bar_str = dbf_name;
		goto normal_exit;
	}


	// prevent the menu from being too long
	tx = (screen_length - 5) - (do_edit * 2 );
	full_menu_count = menu_count;
	if(menu_count > (tx))
			menu_count = tx;

	if(menu_count > 38)
			menu_count = 38;

	// setup the menu
	edit_num = menu_count;
	for( jx = 1; jx <= edit_num; jx++ ) {
		return_str = parse_str('|127QKID=', menu_item_str(menu, jx, 3 ) );
		if(return_str != '')
				menu_set_str( menu, jx, 2, global_str(return_str) );
		return_str = parse_str('|127OFFID=', menu_item_str(menu, jx, 3 ) );
		if(return_str != '')
				menu_set_int( menu, jx, 1, global_int( return_str ) );
		return_str = parse_str('|127CHECKED=', menu_item_str(menu, jx, 3 ) );
		if(return_str != '')
				menu_set_int( menu, jx, 2, global_int( return_str ) );
	}
	++edit_num;
	// add the Edit item if desired
	if (do_edit) {
		if (!top_menu) {
			if(menu_count > 0) {
				++menu_count;
				menu_set_item(menu, menu_count, '', '', '',1, 0, 0 );
			}
		}
		++menu_count;
		edit_num = menu_count;
		menu_set_item(menu, menu_count, '1)edit', '', '/S=0/H=USER', 0, 0, 0 );
	}

	//setup and run the menu
	return_int = menu;
	if( top_menu ) {
		return_str = '';
		if(parse_int('/TP=', mparm_str)) {
			jx = menu_bar_row;
			if(menu_bar_row == 0)
			{
				if((message_row == 1) && global_int("!SHARED_STATUS_LINE") )
				{
					menu_bar_row = message_row;
					set_global_int("!RESET_MENU_BAR",2);
					make_message("");
					jx = 1;
				}
			}
			else
			{
				set_global_int("!RESET_MENU_BAR",1);
			}
			if(jx != 0) {
				return_str = '/X=1/Y=' + str(jx) + '/B=1';
				draw_menu_bar;
			}
		}
		return_str = 'USERIN^TOPMENU ' + return_str;
	} else
			return_str = 'USERIN^SUBMENU ';

	RM(return_str + '/HN=1/H=' + help_ndx + '/#=' + str(menu_count) +
			'/S=' +global_str(menu_name + '?!_0') +'/L=' + lstr + '/G=' +
						menu_name + '?!_' + MParm_Str);

		// if edit was selected then run it
	if((return_int == edit_num) && (do_edit)) {
		kill_box;
		RM('RESEDIT^EDIT_USER_MENU /MN=' + menu_name + '/BUILD=' + str(db_menu) +
					'/BP=1/DBF=' + dbf_name + '/ED=1/TOP=' + str(top_menu));
		if(!return_int)
			goto again;
	}
		// else execute the menu item
	else if((return_int > 0) && (return_int < edit_num )) {

		set_global_str( '@USER@', menu_item_str( menu, return_int, 3 ) );
		RM('user_go');
	}

normal_exit:
	if (!db_menu)
			menu_delete( menu );
	if(return_int > 0)
			return_int = 1000;
	if(parse_int('/TP=', mparm_str)) {
		update_status_line;
		draw_menu_bar;
	}
exit:
	if(global_int("RESET_MENU_BAR") == 2)
	{
		menu_bar_row = 0;
		set_global_int("RESET_MENU_BAR",0);
	}
	set_global_int("RESET_MENU_BAR",0);
}

macro MEMENUS NO_BREAK TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: MEMENUS

Description:  The main menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	set_global_int('MENU_LEVEL', 0);
	push_labels;
	if(  parse_str('/K=', mparm_str) != ''  ) {
		push_key( ascii(parse_str('/K=', mparm_str)), 0 );
	}
	rm('MENU /MN=MAIN/TP=1');
	set_global_int('MENU_LEVEL', 0);
	pop_labels;
}

macro INSTGL TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: INSTGL

Description:  toggles between insert and overwrite modes.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	Insert_Mode = NOT( Insert_Mode );
}

macro BSWORD TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: BSWORD

Description:  This macro deletes the word before the cursor(And you thought it
							had something to do with cow pies).

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	Push_Undo;
	if( read_only )
	{
		return();
	}

	if(  C_Col == 1  ) {
		BACK_SPACE;
		GOTO EXIT;
	}

	if(  at_eol  ) {
		eol;
		if(c_col == 1)
		{
			goto exit;
		}
	}
	LEFT;
	if(xpos(cur_char,'|255|9 ',1))
	{
		call Kill_White_Space;
	}
	else if(  XPOS( cur_char, word_delimits, 1)  ) {
		Call Kill_Delimits;
	//  Call Kill_White_Space;
	//  Call Kill_Word;
	//  Call Kill_White_Space;
	} else {
		Call Kill_Word;
	//  Call Kill_White_Space;
	}
	RIGHT;
	goto EXIT;


kill_white_space:
	if(  (xpos(cur_char,' |9|255',1)) & NOT(at_eol)  ) {
		del_char;
		if(  (C_Col > 1)  ) {
			left;
			Goto KILL_WHITE_SPACE;
		} else {
			Goto EXIT;
		}
	}
	RET;



KILL_WORD:
	if(  xpos(cur_char,word_delimits,1) == 0  ) {
		del_char;
		if(  (C_Col > 1)  ) {
			left;
			Goto KILL_WORD;
		} else {
			Goto EXIT;
		}
		right;
		goto exit;
	}
	RET;

KILL_DELIMITS:
	if((xpos(cur_char,word_delimits,1) != 0) && (xpos(cur_char,'|255|9 ',1) == 0)  ) {
		if(  NOT( at_eol )  ) {
			del_char;
		}
		if(  (C_Col > 1)  ) {
			left;
			Goto KILL_DELIMITS;
		} else {
			Goto EXIT;
		}
	}
	RET;

EXIT:
	Pop_Undo;
}

macro DELWORD TRANS2  {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: DELWORD

Description:  This macro deletes the word after the cursor

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int jx,tr;

	if( read_only )
	{
		return();
	}

	tr = refresh;
	jx = 0;

	Push_Undo;

	if(  (AT_EOL)  ) {
		Del_Char;
		Goto Exit;
	}

	refresh = false;
	if( read_only )
			goto exit;
Delete1:
	/* IF on white space, then delete only to non-white space  */
	if(  xpos(cur_char,' |255|9', 1) != 0  ) {
	delete_white_space:
		while(  NOT(At_Eol) & (xpos(cur_char,' |255|9', 1) != 0)  ) {
			++jx;
			Del_Char;
		}
		if(  jx == 1  ) {
			call delete_word_delimits;
		}
		goto exit;
	}

	++jx;
	if(  (XPOS(Cur_Char,Word_Delimits,1) != 0)  ) {
		call delete_word_delimits;
		GOTO delete_white_space;
	}
	while(  (NOT(AT_EOL)) & (XPOS(Cur_Char,Word_Delimits,1) == 0)  ) {
		++jx;
		Del_Char;
	}
	goto delete_white_space;


delete_word_delimits:
	while(!AT_EOL && XPOS(Cur_Char,Word_Delimits,1)
		&& !xpos(cur_char,'|255|9 ',1) )
	{
		++jx;
		del_char;
	}
	ret;

EXIT:
	refresh = tr;
	Pop_Undo;

}

macro DELEOL TRANS2  {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: DELEOL

Description:  Deletes to the end of a line

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int jx;
	Jx = Length(Get_Line);
	Del_Chars(jx - c_col + 1);
}

macro DEL_TO_HOME TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: DEL_TO_HOME

Description:  Deletes to the home position of the line

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int jx = c_col;
	push_undo;
	home;
	del_chars( jx - c_col );
	pop_undo;
}

macro DEL TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

 NAME:         DEL

 DESCRIPTION:  Deletes character that the cursor is on OR
							 IF persistent blocks are OFF, AND a block is marked, then
							 that block is deleted.

 PARAMETERS:

 RETURNS:

*****************************09-29-91 02:17pm*******************************/
	if( !persistent_blocks && block_stat ) {
		delete_block;
	}
	else {
		del_char;
	}
}

macro HOME TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: HOME

Description:  Implements the home function as follows:

			If the home key is pressed once, then the cursor will go to:
					A.  The current indent level if the cursor is beyond the current
							indent level.
					B.  Column 1 if the cursor is less than or equal to the current
							indent level.

			The following stuff requires that the /T=nn parameter be passed to HOME
			where nn is the number of milliseconds to wait for the next keypress.

			If the home key is pressed twice in rapid succession and the cursor is NOT
					at the top of the window, then the cursor will go to the top of the
					window.  If the cursor is at the top of the window, then the cursor
					will be moved to the top of the file.

			If the home key is pressed three times in rapid succession, then the cursor
					will be moved to the top of the file.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  k1, k2, tm = 18, tc, t ;

	k1 = key1;  k2 = key2;

	PUSH_UNDO;
loop:
	tc = c_col;
	if(  tc > 1  ) {
		first_word;
	}
	if(  (c_col > 1) && (c_col < tc)  ) {
		goto exit;
	}
	if(  (at_eol)  ) {
		if(  tc > indent_level  ) {
			goto_col(indent_level);
		}
	} else {
		if(  c_col >= tc  ) {
			goto_col(1);
		}
	}
	goto_col( c_col );
	tm = Parse_Int( '/T=', MParm_Str );
	if(  ( tm > 0 )  ) {
		tm = tm / 55;
		call delay_check;
		if(  return_int  ) {
home_on:
			if(  (key1 == k1) && (key2 == k2)  ) {
				if(  c_row == 1  ) {
					goto try_tof;
				}
				refresh = FALSE;
				while(  c_row > 1  ) {
					up;
				}
				refresh = TRUE;
				goto_col(1);
				call delay_check;
				if(  return_int  ) {
		 home_on2:
					if((key1 == k1) && (key2 == k2)) {
	try_tof:
						tof;
						goto exit;
					}
				}
			}
			goto push_exit;
		}
	}

	goto exit;

delay_check:
	return_int = 0;
	t = system_timer;
	while(( system_timer - t) < tm) {
		if(check_key) {
			return_int = 1;
			break;
		}
	}
	ret;


push_exit:
	push_key( key1, key2 );

exit:
	POP_UNDO;
}

macro END TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: END

Description:  Implements the end of line function as follows:
			If the end key is pressed once then, the cursor will go to the end of line.

			The following stuff requires that the /T=nn parameter be passed to END
			where nn is the number of milliseconds to wait for the next keypress.

			If the end key is pressed twice in rapid succession and the cursor is NOT
					at the bottom of the window, then the cursor will go to the bottom
					of the window.  If the cursor is at the bottom of the window, then
					the cursor will be moved to the end of the file.

			If the end key is pressed three times in rapid succession, then the cursor
					will be moved to the end of the file.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  k1, k2, wl, tm, t ;

	k1 = key1;  k2 = key2;
	PUSH_UNDO;
loop:
	eol;

	tm = Parse_Int( '/T=', MParm_Str );
	if(  ( tm > 0 )  ) {
		tm = tm / 55;
		call delay_check;
		if(  return_int  ) {
	 end_on:
			if(  (key1 == k1) & (key2 == k2)  ) {
				wl = win_y2 - win_y1 - 1;
				if(  c_row == wl  ) {
					goto try_eof;
				}
				refresh = false;
				while(  c_row < wl  ) {
					down;
				}
				eol;
				if(  at_eof  ) {
					eof;
				}
				refresh = true;
				redraw;
				call delay_check;
				if(  return_int  ) {
			end_on2:
					if(  (key1 == k1) & (key2 == k2)  ) {
	try_eof:
						eof;
						goto exit;
					}
				}
			}
			goto push_exit;
		}
	}

	goto exit;

delay_check:
	return_int = 0;
	t = system_timer;
	while(( system_timer - t) < tm) {
		if(check_key) {
			return_int = 1;
			break;
		}
	}
	ret;


push_exit:
	push_key( key1, key2 );

exit:
	POP_UNDO;
}

macro shift_cursor TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SHIFT_CURSOR

Description:  Handles SAA/CUA complient shifted cursor key use (mark a stream
							block)

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str codes1[20] = '12346789|0',
			codes2[20] = '|71|72|73|75|77|79|80|81|116|115|145|141|132|118|119|117',
			m_str[8];
	int block_mode = parse_int("/M=", mparm_str),
			shift_stat = peek( 0, 0x417) & 0x0f;

	if ( block_mode == 0 )
	{
		block_mode = global_int("@SHFT_CURS_BLCK_MODE");
	}
	if ((block_mode < 1) || (Block_mode > 3))
		block_mode = 3;

	switch (block_mode) {
		case 1 :
			m_str = "MarkBlck";
			key2 = 0;
			break;
		case 2 :
			m_str = "MColBlck";
			key2 = 0;
			break;
		default :
			m_str = "MStrBlck";
	}

	if( !marking )
		rm("MEUTIL2^" + m_str);
  refresh = TRUE;
	pass_key( 0, key2 );
	do {
		if( check_key ) {
			if( key1 == 253 )
					key1 = 0;
			if( xpos( char(key1), codes1, 1 ) && xpos( char(key2), codes2, 1 )) {
				pass_key( 0, key2 );
			} else {
				push_key(key1, key2 );
				break;
			}
		}
    refresh = TRUE;
    goto_col(c_col);
	} while( peek( 0, 0x417) & shift_stat );
	rm("MEUTIL2^" + m_str);
}

macro CR TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: CR

Description:  Performs a carriage return according to INDENT_STYLE:
							0 = None.  Just do a plain carriage return
							1 = Auto.  Indents to first word of previous line.
							2 = Smart.  Uses language specific smart indenting.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str tstr;
	int jx, temp_ins;

	Push_Undo;

		/* If auto-indent then */
	if(  Indent_Style == 1  ) {
		JX = C_COL;
		FIRST_WORD;
		if(  At_EOL == False  ) {
			SET_INDENT_LEVEL;
		}
		GOTO_COL(JX);
		Goto ExitX;
	}
	if(  Indent_Style == 2  ) {
		TStr = Parse_Str('LS=',Global_Str('.'+Get_Extension(file_name)));
		if(  TStr != ''  ) {
			ERROR_LEVEL = 0;
			RM( TSTR + '^' + Copy(TStr,1,3) + '_IND' );
			if(  Error_Level != 0  ) {
				if(  (Error_Level == 5001)  ) {
					Error_Level = 0;
					Make_Message('Smart indent macro NOT found.');
				} else {
					RM('MEERROR');
				}
				Goto ExitX;
			}
			Goto Exit;
		}
		GOTO EXITX;
	}
EXITX:

			/* If in then insert a hard carriage return if one
			 is defined */
	if(   (HARD_CR != '') & (HARD_CR != '|0')  ) {
		Temp_Ins = Insert_Mode;
		Insert_Mode = True;
		Text(Hard_CR);
		CR;
		if(  wrap_stat  ) {
			word_wrap_line( FALSE, TRUE );
		}
		Insert_Mode = Temp_Ins;
	} else {
		jx = c_col;
		CR;
		if(  (insert_mode == false) & (jx >= indent_level)  ) {
			goto_col(indent_level);
		}
	}
EXIT:
	Pop_Undo;
}

macro TAB TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

 NAME:         TAB

 DESCRIPTION:  Handles <TAB>/<SHFTTAB>.  If persistent_blocks is NOT on,
							 then if a block is marked the block will be indented or
							 undented

 PARAMETERS:   /M=0     Tab right
									1     Tab left

 RETURNS:      Nothing.

*****************************09-30-91 01:47pm*******************************/

	int m = parse_int('/M=', mparm_str );

	if( !persistent_blocks && (block_stat != 0)) {
		if( m )
			rm('Meutil2^UndBlk');
		else
			rm('Meutil2^IndBlk');
	}
	else
		if( m )
			tab_left;
		else
			tab_right;

}

macro DATETIME TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: DATETIME

Description:  Writes the current date and time onto the current line.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	 TEXT( DATE + ' ' + STR_DEL(TIME,6,3));
}

/****************************Multi-Edit Macro********************************

 NAME:         CenterLn

 DESCRIPTION:  Centers the current line verticall in the window.

*****************************08-17-93 05:25pm*******************************/
macro CenterLn
{
	int t_c_row = 0;
	int t_refresh = refresh;
	int tl = c_line;
	refresh = false;

	while( (t_c_row != c_row) && (C_Row > ((Win_Y2 - Win_Y1) / 2))  ) {
		t_c_row = c_row;
		Up;
	}
	while( (t_c_row != c_row) && (C_Row < ((Win_Y2 - Win_Y1) / 2))  ) {
		t_c_row = c_row;
		Down;
	}
	goto_line( tl );
	refresh = t_refresh;
}

macro GOTOLINE TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: GOTOLINE

Description:  Prompts for a line number, then moves the cursor to that line.

Parameters:
							/X=   The column to place the prompt box.
							/Y=   The line to place the prompt box.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

		int jx,jj;
		Return_Str = '';
		Return_Int = Global_Int('@GOTOLINE#');
		if(  return_int < 1  ) {
			return_int = 1;
		}
		RM('userin^QUERYBOX /N=1/MIN=1/L=' + parse_str('/Y=', mparm_str) +
					'/C=' + parse_str('/X=', mparm_str) + '/W=12/P=Line number: /T=GO TO LINE/H=GOTOLINE');
		if(  Return_Str == 'TRUE'  ) {
			refresh = TRUE;
			GOTO_LINE(return_int);
			Set_Global_Int('@GOTOLINE#', return_int);
			redraw;
			return_int = 100;
		} else {
			return_int = 0;
		}
}

/*---------------12-30-92 12:39pm-------------------
 Moves the cursor to line l.  Unlike Goto_Line, this
 function moves the cursor as if the up or down
 arrow keys were used.
--------------------------------------------------*/
void cursor_to_line( int l )
{
	int tr = refresh;
	refresh = false;
	if( c_line > l )
	{
		if(( c_line - l) <= (win_y2 - win_y1))
		{
			while ( c_line > l )
			{
				up;
			}
		}
		else
		{
			rm("TOPWIN");
			goto_line(l);
		}
	}
	else if(c_line < l)
	{
		if(( l - c_line) <= (win_y2 - win_y1))
		{
			while ( c_line < l )
			{
				down;
			}
		}
		else
		{
			rm("BOTWIN");
			goto_line(l);
		}
	}
	refresh = tr;
}

macro TOPBLOCK TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: TOPBLOCK

Description:  Take cursor to top of currently defined block

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int tpersist = persistent_blocks;
	persistent_blocks = true;

	if(  Block_Stat != 0  ) {
		cursor_to_line(block_line1);
		if(  Block_Stat != 1  ) {
			Goto_Col( Block_Col1 );
		}
		Redraw;
	}
	persistent_blocks = tpersist;
}

macro ENDBLOCK TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: ENDBLOCK

Description:  Take cursor to bottom of currently defined block

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int tpersist = persistent_blocks;
	persistent_blocks = true;
	if(  Block_Stat != 0  ) {
		cursor_to_line(block_line2);
		if(  Block_Stat != 1  ) {
			Goto_Col( Block_Col2 );
		}
		Redraw;
	}
	persistent_blocks = tpersist;
}

macro TOPWIN TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: TOPWIN

Description:  Take cursor to top of the window

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	while(  (C_Row > 1)  ) {
		Up;
	}
}

macro BOTWIN TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: BOTWIN

Description:  Take cursor to bottowm of the window

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	while(  (C_Row < ((Win_Y2 - Win_Y1) - 1))  ) {
		Down;
	}
}

macro SCROLLUP TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SCROLLUP

Description:  Scroll screen up one line

Parameters:   /MC=1 move cursor with scrolled line if possible

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  jx ;
	jx = refresh;
	refresh = false;
	Goto_Line(C_Line + 1);
	if (Parse_Int('/MC=',MParm_Str)) {
		if (C_Row > 1) {
			Up;
		}
	}
	Redraw;
	refresh = jx;
}

macro SCROLLDN TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SCROLLDN

Description:  Scroll screen down one line

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  jx ;
	jx = refresh;
	refresh = false;
	Goto_Line( C_Line - 1 );
	if (Parse_Int('/MC=',MParm_Str)) {
		if (C_Row < (Win_Y2 - Win_Y1 - 1)) {
			Down;
		}
	}
	Redraw;
	refresh = jx;
}

macro PAGEBRK TRANS {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: PAGEBRK

Description:  Insert a page break

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	push_undo;
	UP;
	EOL;
	CR;
	GOTO_COL(1);
	TEXT( PAGE_STR );
	UP;
	DOWN;
	pop_undo;
}

macro FIND_EXT {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: FIND_EXT

Description: Makes sure extension specific setup globals are loaded for
	the passed extension.

Parameters:   /EXT=str    Str is the extension to use (including the
													leading ".").

Returns:
			RETURN_STR contains the name of the global to use to access
			the extension specific setup.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str Ext[8];

	Ext = Caps(Parse_Str('/EXT=',MParm_Str));

	if(  (Global_Str(Ext) == '')  ) {
		RM('GET_DB_RECORD /DBF=EXT/F=MECONFIG/NDF=1/DPT=EXT.DB/GLO=' + Ext +
			"/FV=" + make_literal(Copy(Ext,2,3)) + " |,|;|");
		if (return_int < 1) {
// The regular expressions in /FV= allow multiple file extensions per record
// example C;INC;H
			RM('GET_DB_RECORD /DBF=EXT/F=MECONFIG/NDF=1/DPT=EXT.DB/GLO=' + Ext +
				"/FV=[~]*[ ,;]" + make_literal(Copy(Ext,2,3)) + " |,|;|");
		}
//    RM('GET_DB_RECORD /DBF=EXT/F=MECONFIG/NDF=1/DPT=EXT.DB/GLO=' + Ext + '/FV=' + Copy(Ext,2,3));
		if(  (Return_Int < 1)  ) {
				RM('GET_DB_RECORD /DBF=EXT/F=MECONFIG/NDF=1/DPT=EXT.DB/GLO=' + Ext + '/FV=DEFAULT');
			Return_Int = -1;
		} else {
			Return_Int = 0;
		}
		if(  xpos( ext + '', Global_Str('EXTENSIONS'), 1 ) == 0  ) {
			Set_Global_Str('EXTENSIONS',Global_Str('EXTENSIONS') + Ext + '');
		}
	} else {
		Return_Int = 0;
	}
EXIT:
	return_str = ext;
}

macro SETCONFIG  {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SetConfig

Description: Loads up the specified DB file (if it is not already
						loaded), and then searches for the specified page title.

Returns:    RETURN_INT = 1 IF the title was found,
												 0 If NOT found.

Parameters:   /DB=str   The db file name.
							/T=str    The page Title.
							/C=int    1 = Create page if not found.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str  tstr[40];
	int Path_Specified,l,c,r,o;
	int tRegExpStat = reg_exp_stat,   // JKP make sure these get restored
			tCase = ignore_case;

	return_str = parse_str('/DB=', mparm_str);
	if(  (Get_extension(Return_Str) == '')  ) {
/* This provides support for a default DB file extension to be configured.  It
is overridden if the calling macro specifies an extension */
		if(  (Global_Str('@DB_EXTENSION') == '')  ) {
			Return_Str = Return_Str + '.DB';
		} else {
			Return_Str = Return_Str + '.' + Global_Str('@DB_EXTENSION');
		}
	}
	Path_Specified = (Get_Path(Return_Str) != '');
	if(  ((user_id == '') | (Path_Specified == True))  ) {
		tstr = '';
	} else {
		tstr = me_path + user_id + '.USR\';
	}
	if(  switch_file( CAPS( tstr + return_str ) )  ) {
		return_str = tstr + return_str;
		error_level = 0;
	} else {
		if(  (Path_Specified == False)  ) {
			RM('MakeUserPath /DF=1');
		}
		error_level = 0;
		if(  NOT( switch_file( return_str ) )  ) {
			Switch_Window( window_count );
			Create_Window;
			Load_File( Return_Str );
			window_attr = $81;
		}
	}
	RETURN_INT = 1;
	if(  error_level == 0  ) {
		tstr = caps(parse_str('/T=', mparm_str ));
		reg_exp_stat = TRUE;
		ignore_case  = TRUE;    // JKP added to take care of titles in lower case
		if(  tstr != ''  ) {
			if( global_str( '~SETCONFIG_' + str( window_id ) ) == tstr ) {
				GET_MARK_RECORD( 1, 3, l, c, r, o );
				goto_line( l );
				goto_col( 1 );
				if(  find_text( '%|12' + make_literal(tstr) + '$|| ' ,1,_OldExp)) {
					return_int = TRUE;
					goto exit;
				}
			}
			tof;
			if(  NOT(Find_Text( '%|12' + make_literal(tstr) + '$|| ' ,0,_OldExp))  ) {
				RETURN_INT = 0;
				if(  parse_int( '/C=', mparm_str )  ) {
					EOF;
					Insert_mode = true;
					if(  c_col > 1  ) {
						CR;
					}
					TEXT( '|12' + tstr );
					RETURN_INT = 1;
				}
			}
			if( return_int ) {
				set_global_str( '~SETCONFIG_' + str( window_id ), tstr );
				set_mark_record( 1, 3, c_line, c_col, 1, 1 );
			}
		} else {
			tof;
		}
	} else {
		RETURN_INT = 0;
	}
exit:
	reg_exp_stat = tRegExpStat;
	ignore_case  = tCase;
}

macro GET_DB_RECORD {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: GET_DB_RECORD

Description:  Finds and returns the specified record in a .DB file.

Parameters:
						/F=     Filename
						/NDF=1  Do not delete the window after you are done
						/DPT=   Data page title
						/GLO=   Name of global string to store record
						/DBF=   Field name
						/FV=    Value of field
						/CS=1   Make search for field case sensitive
						/#=     Number of Record to return.  Overrides /DBF= and /FV=.
											Example:  If /#=3 then the 3rd record will be returned.
						/AGLO=  Name of global integer to store total amount of records
										found.  This was added primarily for the macro COMPILE so
										that it can find out it there is only 1 compiler interface
										and bypass the compiler menu.

Returns:
						Return_int = 1 if found
						Return_int = 0 if not found, but first record is stored in /GLO=
						Return_int = -1 if not found and there are no records.
						Return_Int = -2 if the DPT is not found in the DB file.
						Return_Int = -3 if the DB file is not found.

This only works for string and integer fields

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str F_Name[80] = Caps(Parse_Str('/F=',MParm_Str)),
					DPT[80] = Caps(Parse_Str('/DPT=',MParm_Str));
	int Active_Window = Window_Id,
					Db_Window = 0,
					T_Reg_Exp_Stat = Reg_Exp_Stat,
					T_Ignore_Case = Ignore_Case,
					T_Refresh = Refresh,
					Start_Records,
					Record_Amount,
					Rec_Num = Parse_Int('/#=',Mparm_Str);

	ignore_case = true;
	char Delimit = Parse_Str('/LD=',MParm_Str);
	if(  (Delimit == '|0')  ) {
		Delimit = '';
	}
	Refresh = False;

	RM('Setconfig /DB=' + F_Name + '/T=' + DPT);
	F_Name = Return_Str;
	db_window = window_id;
	if(  (Truncate_Path(F_Name) != Truncate_Path(File_Name))  ) {
			Return_Int = -3;
			Goto EXIT;
	}
	if (Svl(Dpt)) {
		if (Caps(Get_Line) != ('|12' + DPT)) {
			Return_Int = -2;
			Goto EXIT;
		}
	}

	reg_exp_stat = TRUE;
	Start_Records = C_Line;
	Eol;
	if (Find_Text('%{|12}||{@*@*@*@*START@*@*@*@*}',0,_OldExp)) {
/* If we found a data page title, assume no header in this db */
		if (Cur_Char != '|12') {
			Start_Records = C_Line;
			if(  (Find_Text('%|12',0,_OldExp) == False)  ) {
				Eof;
				if(  (C_Col > 1)  ) {
					Down;
				}
			}
		}
	} else {
		Eof;
		if(  (C_Col > 1)  ) {
			Down;
		}
	}
	++Start_Records;
	Record_Amount = C_Line - Start_Records;
	Set_Global_Int(Parse_Str('/AGLO=',MParm_Str),Record_Amount);
	if(  (Record_Amount < 1)  ) {
		Return_Int = -1;
		Goto EXIT;
	}
	if(  (Rec_Num)  ) {
		if(  ((Rec_Num > Record_Amount) | (Rec_Num < 1))  ) {
			Return_Int = 0;
			Goto_Line(Start_Records);
		} else {
			Return_Int = 1;
			Goto_Line(Start_Records + Rec_Num - 1);
		}
	} else {
		Goto_Line(Start_Records);
		Goto_Col(1);
		if (parse_int("/CS=",Mparm_Str)) { // case sensitive field search
			ignore_case = false;
		}
		reg_Exp_Stat = TRUE;
		Return_Int = Search_Fwd(Delimit + Parse_Str('/DBF=',MParm_Str) + '=' +
									Parse_Str('/FV=',MParm_Str)  + Delimit + '||$',Record_Amount);
	}
	Set_Global_Str(Parse_Str('/GLO=',MParm_Str),Get_Line);

EXIT:
	if(  (Parse_Int('/NDF=',MParm_Str) == False)  ) {
		if(  (Switch_Win_Id(DB_Window))  ) {
			Delete_Window;
		}
	}
	Reg_Exp_Stat = T_Reg_Exp_Stat;
	Ignore_Case = T_Ignore_Case;
	Switch_Win_Id(Active_Window);
	Refresh = T_Refresh;
}


/******************* Multi-Edit STRING Macro Function ***********************

 NAME:         GetUserMacroPath()

 DESCRIPTION:  Returns the path to place user generated .MAC files in.

*****************************07-23-93 11:15am*******************************/
str GetUserMacroPath()  trans2
{
	if( user_path != me_path )
	{
		return(user_path);
	}
	else
	{
		return(me_path + 'MAC\');
	}
}


/****************************Multi-Edit Macro********************************

 NAME:         GetUserPath

 DESCRIPTION:  Gets the complete path pointing to the user directory, or
							 the Multi-Edit directory if user directories are not being
							 used.

 PARAMETERS:   if create_dirs is TRUE then it will create the user directory
							 if it doesn't exist.

 RETURNS:      A string containing the complete path name including ending
							 backslash.

*****************************12-21-92 02:05pm*******************************/
str GetUserPath( int create_dirs = FALSE )
{

	return( user_path );
	/*
	str tstr[128] = global_str("!USER_FILES_PATH");

	if( svl(tstr) == 0 )
	{
		tstr = ME_Path;
	}
	if(create_dirs)
	{
		if ( svl(tstr) != 3 )    // We don't have a root drive i.e. "X:\"
		{
			if(str_char(tstr,svl(tstr))  == "\\")
				tstr = copy(tstr,1, svl(tstr)-1);
			if( first_file(tstr) != 0)
			{
				if(  MkDir( tstr ) != 0  ) {
					error_level = 3003;
					return(me_path);
				}
			}
		}
	}
	if(str_char(tstr, svl(tstr)) != "\\")
	{
		tstr += "\\";
	}

	if(  user_id != ''  ) {
		tstr += user_id + '.USR';
		if(create_dirs)
		{
			if( first_file(tstr) != 0)
			{
				if(  MkDir( tstr ) != 0  ) {
					error_level = 3003;
					return(me_path);
				}
			}
		}
		tstr += "\\";
	}
	return(tstr);
	*/
}


macro MAKEUSERPATH {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: MakeUserPath

Description: Takes the filename in RETURN_STR and converts it to a
	complete path and filename for the current USER_ID.  Automatically creates
	a .USR subdirectory if one doesn't exist for the current USER_ID.

Parameters: /DF=n   1 = Copy default file if specified file doesn't exist.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  t_attr, jx ;
	str  tstr[120], t_search_rec[64] ;

	Return_Str = Truncate_Path( return_str );
	t_attr = File_Search_Attr;
	t_search_rec = file_search_rec;

	File_Search_Attr = $37;

	tstr = GetUserPath( FALSE );
	return_str = tstr + return_str;

	if( first_file( return_str ) != 0 )
	{
		return_str = GetUserPath( TRUE ) + truncate_path( return_str );
		File_Search_Attr = $27;
		if(  (first_file( return_str ) != 0)  )
		{
			if(  parse_int('/DF=',mparm_Str)  )
			{
				if(  get_path( return_str) != me_path )
				{
					jx = Copy_File( me_path + truncate_path(return_str), return_str, false );
				}
			}
		}
	}

	file_search_rec = t_search_rec;
	File_Search_Attr = t_attr;
}

macro TMP_FILE_NAME trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: TMP_FILE_NAME

Description:  Returns in RETURN_STR a file name with path suitable for placing
							any .TMP files like MEERR.TMP in the proper directory according
							to current configuration options.

Parameters:
							/FN=str    The filename to use (no path).

							 (C) Copyright 1993 by American Cybernetics, Inc.
*******************************************************************************/
	str t_name = parse_str("/FN=",mparm_str);

	return_str = Global_Str('@TMP_FILE_PATH');
	if(length(return_str) != 0) {
		RM('XLATECMDLINE');
		if(copy(return_str,length(return_str),1) != '\') {
			return_str += '\';
		}
		return_str += user_id + t_name;
	} else {
		return_str = user_id + t_name;
		rm('MakeUserPath');
	}
	return_str = fexpand(return_str);
}

macro XLATECMDLINE TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: XlateCmdLine

Description:  Translates RETURN_STR so that any occurances of
							<FILE>, <NAME>, <EXT>, <PATH>, <ME_PATH>, <USER_PATH>, <MAC_PATH>
							 are expanded out.

							Also, <%str> is now expanded out to whatever the environment
							variable "str" is.
							AND, <~str> is now expanded out to the value of
							Global_Str( str ).

Parameters:
							/F=str    The filename to use.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str  fname[128] ;
	int  jx, jy ;
	fname = parse_str('/F=', mparm_str );

	while( JX = XPOS('<FILE>',Caps(return_str),1) )
	{
		return_str = Str_Del(return_str,jx,6);
		return_str = Str_Ins(Truncate_Extension(fname),return_str,jx);
	}

	while( JX = XPOS('<EXT>',Caps(return_str),1) )
	{
		return_str = Str_Del(return_str,jx,5);
		return_str = Str_Ins(Get_Extension(fname),return_str,jx);
	}

	while( JX = XPOS('<NAME>',Caps(return_str),1) )
	{
		return_str = Str_Del(return_str,jx,6);
		return_str = Str_Ins(Truncate_Path(Truncate_Extension(fname)),return_str,jx);
	}

	while( JX = XPOS('<PATH>',Caps(return_str),1) )
	{
		return_str = Str_Del(return_str,jx,6);
		return_str = Str_Ins(Get_Path(fname),return_str,jx);
	}

	while( JX = XPOS('<ME_PATH>',Caps(return_str),1) )
	{
		return_str = Str_Del(return_str,jx,9);
		return_str = Str_Ins(me_path,return_str,jx);
	}

	while( JX = XPOS('<COMSPEC>',Caps(return_str),1) )
	{
		return_str = Str_Del(return_str,jx,9);
		return_str = Str_Ins(comspec,return_str,jx);
	}

	while( JX = XPOS('<USER_PATH>',Caps(return_str),1) )
	{
		return_str = Str_Del(return_str,jx,11);
		return_str = Str_Ins(GetUserPath(false),return_str,jx);
	}

	while( JX = XPOS('<MAC_PATH>',Caps(return_str),1) )
	{
		return_str = Str_Del(return_str,jx,10);
		return_str = Str_Ins(GetUserMacroPath(),return_str,jx);
	}

	while( JX = XPOS('<USER_ID>',Caps(return_str),1))
	{
		return_str = Str_Del(return_str,jx,9);
		return_str = Str_Ins(User_id,return_str,jx);
	}

	while( jx = XPOS('<%',return_str,1) ) {
		return_str = Str_Del(return_str,jx,2);
		jy = XPOS( '>', return_str, jx );
		if( jy > 0 ) {
			fname = copy( return_str, jx, jy - jx );
			return_str = Str_Ins(get_environment( fname ),
						Str_Del(return_str, jx, jy - jx + 1),
						jx);
		}
	}

	while( jx = XPOS('<~',return_str,1) ) {
		return_str = Str_Del(return_str,jx,2);
		jy = XPOS( '>', return_str, jx );
		if( jy > 0 ) {
			fname = copy( return_str, jx, jy - jx );
			return_str = Str_Ins(global_str( fname ),
						Str_Del(return_str, jx, jy - jx + 1),
						jx);
		}
	}

}

macro SETTABS {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SETTABS

Description: Sets up even interval tab spacing.

Parameters: /TS=nn    Tab spacing.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  Tab_Size, jx, margin ;
	str  tformat[2048];
	Tab_Size = Parse_Int('/TS=', mparm_str );
	Margin = Parse_Int('/RM=', mparm_str );
	if(  (Margin < 1) | (Margin > 2048)  ) {
		Margin = 128;
	}
	TFormat = ' ';
	while(  SVL(TFormat) < Margin  ) {
		Jx = 1;
		while(  jx < Tab_Size  ) {
			TFormat = TFormat + ' ';
			++jx;
		}
		TFormat = TFormat + '|16';
	}
	Format_Line = TFormat;
	Format_Stat = False;
}


macro EXTSETUP {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: EXTSETUP

Description:  Is called to setup a window/file based on the files extension.
						If the extension is not found in the global extension list, then
						the default extension setup is used.

Parameters:
							/PRE=  If 1, then post-load macro will not be run, such as would
							be the case if this was run after the file had already been loaded.

							/FTO=x File type override
										1 = DOS
										2 = UNIX
										3 = Binary
									255 = Do NOT override the line_terminator value or the
												fixed line length value

Returns:  Return_Str = default directory for file.
							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  t_cb_t_color = cb_t_color,
					post = parse_int('/POST=', mparm_str)
					 ;
	str TStr1[2048],TStr2,Tstr3;
	int JX,jy,T_File_Changed = File_Changed;

	if(  xpos('/EXT=', mparm_str, 1) != 0  ) {
		tstr3 = parse_str('/EXT=', mparm_str );
	} else {
		tstr3 = get_extension( file_name );
	}
	RM( 'FIND_EXT /EXT=.' + tstr3 );
	Tstr3 = Return_Str;
	TStr1 = Global_Str(tstr3);

	Wrap_Stat = Parse_Int('WW=', TStr1 );
	indent_style = Parse_Int('IND=', TStr1 );
	if(  (Indent_Style > 0)  ) {
		Indent_Style = Indent_Style - 1;
	}
	Doc_Mode = Parse_Int('DOC=', TStr1 );

	if(  post == 0  ) {
		jx = parse_int('/FTO=',mparm_str);

		if(jx != 255 )
		{
			fixed_line_length = 0;
			if(jx == 0)
				jx = parse_int('FT=', tstr1 );

			if(  jx == 2  ) {
				line_terminator = '|10|0';
			} else if(  jx == 3  ) {
				line_terminator = '|0';
				fixed_line_length = parse_int('BRL=', tstr1);
				if(fixed_line_length == 0)
				{
					fixed_line_length = 16;
				}
			} else {
				line_terminator = '|13|10';
			}
		}
	}
	if(  Format_Stat == 0  ) {
		if(  (Parse_Int('UF=',TStr1) == 0)  ) {
			JX = Parse_Int('TS=',TStr1);
			if(  (JX != 0)  ) {
				Use_Format_Line = false;
				right_margin = Parse_Int('RM=',TStr1);
				if(  (right_margin == 0)  ) {
					Right_Margin = 80;
				}
				if( ( post == 0 ) || (format_line == "") ) {
					RM('SETTABS /TS=' + str( jx ) + '/RM=' + str(right_margin) );
	/* Here, we are deleting any defined format line from the global to conserve ram */
					Jx = XPos('FORMAT=',Tstr1,1);
					if(  (Jx)  ) {
						Jy = Length(Parse_Str('FORMAT=',Tstr1));
						Set_Global_Str(TStr3,Str_Del(TStr1,Jx,Jy + 8));
					}
				}
			} else {
				Goto USE_FORMAT;
			}
		} else {
USE_FORMAT:
			TStr2 = Parse_Str('FORMAT=',TStr1);
			if(  Tstr2 != ''  ) {
				Format_Line = TStr2;
				Format_Stat = false;
				Use_Format_Line = true;
			}
			if(  (XPos('R',Caps(Format_Line),1) == 0)  ) {
/* If there is no right_margin defined in the format line, see if there is one
defined explicity */
				Right_Margin = Parse_Int('RM=',TStr1);
			}
		}
	}
	tstr3 = parse_str('CC=', tstr1 );
	if(  xpos('/CTC=', tstr3, 1) != 0  ) {
		t_color = parse_int( '/CTC=', tstr3);
		c_color = parse_int( '/CCC=', tstr3);
		b_color = parse_int( '/CBC=', tstr3);
		h_color = parse_int( '/CHC=', tstr3);
		s_color = parse_int( '/CSC=', tstr3);
		l_color = parse_int( '/CLC=', tstr3);
		lb_color = parse_int( '/CLBC=', tstr3);
		eof_color = parse_int( '/CEC=', tstr3);
		window_color_stat = TRUE;
	}
	else {
		t_color = w_t_color;
		c_color = w_c_color;
		b_color = w_b_color;
		h_color = w_h_color;
		s_color = w_s_color;
		l_color = w_l_color;
		lb_color = w_lb_color;
		eof_color = w_eof_color;
		window_color_stat = FALSE;
	}
	if( parse_int('ATX=', tstr1 ) )
		key_to_window( <SPACE>, 'LANGUAGE^TEMPLATE /S=1' );
	else
		key_to_window( <SPACE>, '' );

	if ( Parse_Int('/PRE=',MParm_Str)==0 )
	{
		TStr3 = Parse_Str('LS=',tstr1);
		RM(Copy(tstr3,1,8)+'^'+Copy(tstr3,1,3)+'SETX');
		if (!parse_int("/NOSYNTAX=",Mparm_Str)) {
			RM("SET_SYNTAX_PARMS");
		}
		error_level = 0;
		TStr3 = Parse_Str('MAC=',TStr1);
		if ( ''!=TStr3 )
		{
			int fsa=File_Search_ATTR;
			str fsr[80]=File_Search_REC;
			File_Changed = T_File_Changed;
			RM(TStr3);
			T_File_Changed = File_Changed;
			File_Search_ATTR=fsa;
			File_Search_REC=fsr;
		}
	}
	if(post && (copy(line_terminator,1,1) == '|0')  && !(window_attr & 0x20))
	{
		if(global_int("BINARY_MODE"))
			rm("hex^HexWindow");
	}
	File_Changed = T_File_Changed;
}

macro SET_SYNTAX_PARMS {
/*
*/
	str language[40] = parse_str("/LANG=",mparm_str),
			t_str, sc1, ac1[128] = "",
			rw1,rw2,rw3,rw4;
	int t_int,
			ta,
			reinit = parse_int("/REINIT=",mparm_str),
			reset =  parse_int("/RESET=", mparm_str),
			parse_mode = 0,
			case_sensitive;
	char tc;

	if (!svl(language))
		language = parse_str("LS=",Global_Str("." + Get_Extension(file_name)));
	else
		reinit = true;

	if (svl(language)) {
	str
			g_name[20] = "@" + copy(language,1,3) + "_SYNTAX_PARMS",
			g_str[2048],
			attr_str[80] = global_str("@SYNTAX_COLORS");


		if ((global_str(g_name) == "") || RESET) {
			RM("GET_DB_RECORD /F=MECONFIG/NDF=1/DPT=LANGUAGE.DB/DBF=LANG/FV=" + language
				+ "/GLO=" + g_name);
		}
		g_str = global_str(g_name);
		var_remove_str("LANG=",g_str);
		if(svl(g_str) != 0)
		{
			if(g_str != "RECORD_SET")
			{
	// if we are not case sensitive, capitalize all keywords
				case_sensitive = parse_int("CS=",g_str);
				rw1 = remove_space(parse_str("RW1=",g_str));
				if (!case_sensitive)
					rw1 = caps(rw1);
				rw2 = remove_space(parse_str("RW2=",g_str));
				if (!case_sensitive)
					rw2 = caps(rw2);
				rw3 = remove_space(parse_str("RW3=",g_str));
				if (!case_sensitive)
					rw3 = caps(rw3);
				rw4 = remove_space(parse_str("RW4=",g_str));
				if (!case_sensitive)
					rw4 = caps(rw4);
	// parse out first character of each of the keywords fields
				t_str = rw1;
				t_int = 2;
				call PARSE_SCAN_CHARS;
				t_int = svl(t_str) + 1;
				t_str = t_str + rw2;
				call PARSE_SCAN_CHARS2;
				t_int = svl(t_str) + 1;
				t_str = t_str + rw3;
				call PARSE_SCAN_CHARS2;
				t_int = svl(t_str) + 1;
				t_str = t_str + rw4;
				call PARSE_SCAN_CHARS2;
				sc1 = t_str;

				t_int = 2;
				t_str = /* t_str + */
				copy(parse_str("OC1=",g_str),1,1) +
				copy(parse_str("CC1=",g_str),
								length(parse_str("CC1=",g_str)),1) +
				Copy(parse_str("OC2=",g_str),1,1) +
				copy(parse_str("CC2=",g_str),
								length(parse_str("CC2=",g_str)),1) +
				parse_str("SYM=",g_str) +
				parse_str("STR=",g_str) +
				parse_str("EOL=",g_str) +
				parse_str("EOL2=",g_str)
				;
				parse_mode = 1;
				call PARSE_SCAN_CHARS2;
				set_keywords(copy(language,1,3) + "_KEYS",   // name
					case_sensitive,// TRUE = case sensitive
					sc1,
					t_str, // + parse_str("SYM=",g_str),  // scan chars
					//  reserved words 1
					" " + RW1 + " ",
					// reserved words 2
					" " + RW2 + " ",
					// reserved words 3
					" " + RW3 + " ",
					// reserved words 4
					" " + RW4 + " ",
					ac1,
					parse_int("/RWC=",attr_str),  // reserved words attr
					parse_str("SYM=",g_str), //Symbols
					parse_int("/SYC=",attr_str), // symbol attribute
					parse_str("STR=",g_str), //string chars,
					parse_int("/SCC=",attr_str), //string attr
					parse_str("LIT=",g_str), // literal char
					parse_str("EOL=",g_str), // eol comment
					parse_int("/ECC=",attr_str),  // eol comment attr
					parse_int("EOC=",g_str ), // eol column
					parse_str("EOL2=",g_str), // eol comment
					parse_int("/ECC=",attr_str),  // eol comment attr
					parse_int("EOC2=",g_str ), // eol column
					parse_str("OC1=",g_str), //open comment 1
					parse_str("CC1=",g_str), //close comment 1
					parse_int("/C1C=",attr_str),  // open/close comment 2 attr
					parse_str("OC2=",g_str), //open comment 2
					parse_str("CC2=",g_str), // close comment 2
					parse_int("/C2C=",attr_str), // open/close comment 2 attr
					parse_str("NC=",g_str), // numeric chars
					parse_int("/NCC=",attr_str) // numeric chars attr
					);
				set_global_str(g_name,"RECORD_SET");
			}
			if (!reinit)  {
				keywords_str = copy(language,1,3) + "_KEYS";
			}
		}
	}
	goto EXIT;

PARSE_SCAN_CHARS:
	while (((tc = str_char(t_str,t_int)) != " ") && (t_int <= svl(t_str))) {
		t_str = str_del(t_str,t_int,1);
			// TMJ - 10-30-92 10:51am - Locate NON-ALPHA-NUMERIC chars
		if( !xpos( caps(tc),  "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_",1)) {
			if ( !xpos( tc, ac1,1) )
			{
				ac1 += tc;
			}
		}
	}
	if (t_int < svl(t_str)) {
		t_str = str_del(t_str,t_int,1); // remove the space
PARSE_SCAN_CHARS2:
		if (xpos(str_char(t_str,t_int),copy(t_str,1,t_int - 1),1)) {
//if this char is already in the string, delete it
			t_str = str_del(t_str,t_int,1);
		} else if ((t_int < svl(t_str)) || (parse_mode))
			++t_int;
		if (parse_mode) {
			if (t_int > svl(t_str))
				RET;
			goto PARSE_SCAN_CHARS2;
		}
		goto PARSE_SCAN_CHARS;

	}
	RET;

EXIT:
}

macro PATHSEARCH TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: PathSearch

Description: Searches for a filename in a given path
						XLateCmdLine is called to parse out any metacommands BEFORE
						the search takes place.

Parameters:  /F=str     The filename

						RETURN_STR contains the path. For example:  "\;C:\ME;C:\MASM"

Returns:  RETURN_STR = complete path and filename

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str  fname[20], fpath[120] ;
	int  jx, jy ;
	fname = parse_str('/F=',mparm_str);
	rm("XlateCmdLine /F=" + fname );
	jy = 0;
	fpath = '';
	goto first_time;
LOOP:
	if(  jy > length(return_str)  ) {
		RETURN_INT = 0;
		goto exit;
	}
	CALL parse_path;
first_time:
	if(  file_exists( fpath + fname )  ) {
		return_int = 1;
		return_str = fexpand(fpath + fname);
		goto exit;
	}
	goto loop;

	Parse_Path:
		jx = XPos( ';', return_str, jy + 1 );
		if(  jx == 0  ) {
			jx = LENGTH( return_str ) + 1;
		}
		fpath = Copy( return_str, jy + 1, jx - jy - 1 );
		if(  (SVL(fpath) > 0) & (copy(fpath,SVL(fpath),1) != '\')  ) {
			fpath = fpath + '\';
		}
		jy = jx;
		RET;

EXIT:
}


macro SETFILENAME TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SETFILENAME

Description:  Completes a filename (no wildcards).  Adds in the default
	directory for that extension if none is specified.

Parameters:   Return_Str = Filename

Returns:      Return_Str = new Filename
							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str  tstr[128], gstr[20] ;
	int  jx ;

	tstr = return_str;
	if(  get_path(tstr) == ''  ) {
		RM( 'FIND_EXT /EXT=.' + get_extension( tstr ) );
		gstr = return_str;
		return_str = parse_str('DIR=',Global_Str(gstr));
		RM('PathSearch /F=' + tstr);
		if(  return_int == 0  ) {
			return_str = parse_str('DIR=',Global_Str(gstr));
			rm("XlateCmdLine /F=" + tstr );
			jx = xpos(';', return_str, 1);
			if(  jx == 0  ) {
				jx = length(return_str) + 1;
			}
			return_str = copy(return_str,1, jx - 1);
			if(  (Length(return_str) > 0) & (copy(return_str,Length(Return_Str),1) != '\')  ) {
				return_str = return_str + '\';
			}
			return_str = return_str + tstr;
		}
	}
Expand_Exit:
	return_str = fexpand( return_str );
}

macro CREATEWINDOW {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: CreateWindow

Description: Creates a new window at the end of the window list.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  jx, tr, x1, x2, y1, y2, ox1,oy1 ;
	str  tstr[40] ;
	tr = refresh;
	refresh = FALSE;

	jx = cur_window;
	Switch_Window(Window_COunt);
	Error_Level = 0;
	CREATE_WINDOW;
	if(  error_level == 0  ) {
		x1 = min_window_col;
		x2 = max_window_col;
		y1 = min_window_row;
		screen_num = global_int('@SCREEN_COUNT') + 1;
		if(  screen_num <= 1  ) {
			screen_num = 2;
		}
		jx = (global_int( '~WIN_BOTTOM_BORDER') == 0) * (global_int('ICON_ROWS') != 0);
		y2 = max_window_row - Global_Int( 'ICON_ROWS') - jx;
		set_global_int('@SCREEN_COUNT', screen_num );
		tstr = global_str('@INIT_WINDOW_PARMS');
		if(  tstr != ''  ) {
			x1 = parse_int('/X1=', tstr);
			x2 = parse_int('/X2=', tstr);
			y1 = parse_int('/Y1=', tstr);
			y2 = parse_int('/Y2=', tstr);
			if(  x1 < min_window_col  ) {
				x1 = min_window_col;
			}
			if(  (x2 == 0) | (x2 > max_window_col)  ) {
				x2 = max_window_col;
			}
			if(  y1 < min_window_row  ) {
				y1 = min_window_row;
			}
			if(  (y2 == 0) | (y2 > (max_window_row - Global_Int( 'ICON_ROWS') - jx)  )) {
				y2 = max_window_row - Global_Int( 'ICON_ROWS') - jx;
			}
		}
		size_window( x1, y1, x2, y2 );
	} else {
		switch_window(jx);
	}

	refresh = TR;
}

macro ldFiles trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: LDFILES

Description:  Loads files into windows.  Handles wild card characters.
						Normally, LDFILES checks to see if the file is already loaded.

Parameters: RETURN_STR = the filename;
						/LC=load_count
						/MC=max_count
						/NW=1     no wildcards allowed.
						/NC=1     no check for the file already loaded.
						/PRE=   Will pass this parameter to EXTSETUP
						/CW=1   create new window immediately, regardless of /LC=
								2   create new window ONLY if the current window is NOT empty.
						/NHA=1  No History Add - don't add filenames loaded to history list.
						/DE=1   Use default extension if applicable
						/FTO=x  Filetype override
										1 = DOS
										2 = UNIX
										3 = Binary
										255 = Leave line_terminator values alone
						/TAG=x  Load using tag list in specified window id   // ldh //tmj

						If LDFILES is entered with LOAD_COUNT
						set to zero, then the first file will be loaded into the current
						window.  Subsequent files will be loaded into new windows.

Returns:    Return_Int = LOAD_COUNT.

						Also, Global integer LOAD_COUNT will be set to /LC=nn + the
						number of files loaded.

Note: Changes marked with "// ldh" were made 13-Sep-93 to allow loading files
			from a tag list.  Need to support tagging file in file_prompt. // ldh

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  Load_Count, lc2 , good_load, Jx, Jy;
	str  Tstr[128], TStr2[128], t_search_rec[64] ,MSTr[128];
	int  t_win, tr, max_count, wild_cards, load_check,T_File_Search_Attr ;
	int  xload = FALSE, fto = parse_int("/FTO=",mparm_str),
			 tag = parse_int("/TAG=", mparm_str);                 // Tmj

	good_load = False;
	t_search_rec = file_search_rec;
	T_File_Search_Attr = File_Search_Attr;
	File_Search_Attr = $27;

	TStr = return_str;

	load_check = (parse_int('/NC=', mparm_str) == 0);
	wild_cards = (parse_int('/NW=', mparm_str) == 0);
	Load_Count = parse_int('/LC=',mparm_str);
	max_count = parse_int('/MC=',mparm_str);
	if(  max_count == 0  ) {
		max_count = 1000;
	}
	lc2 = parse_int('/CW=', mparm_str );
	if ( lc2 == 2 )
	{
		lc2 = 1;
		if(  (caps(file_name) == '?NO-FILE?') & (file_changed == FALSE)  ) {
			if(  at_eof  ) {
				mark_pos;
				tof;
				if(  at_eof  ) {
					lc2 = 0;
				}
				goto_mark;
			}
		}
	}
// <<< Added 13-Sep-93 ldh ****************************************************
	if (tag) {                                               // Tmj

		int ow = window_id,
				new_win = ow,
				srf = refresh;

		refresh = FALSE;
		if(switch_win_id(tag))                                // Tmj
		{
			tof;
			tstr = shorten_str(get_line);
			if (tstr != "") {                                // Tmj
				// lc2 = 1;                                    // Tmj
				if ((parse_int("/NHA=", mparm_str) == 0)) {
					rm("userIn^history_list /M=1/HISTORY=FILE_HISTORY");
				}

				int sfto = fto;
				while (TRUE) {
					if (jx = xpos("/FTO=", tstr, 1)) {
						fto = parse_int("/FTO=", tstr);
						tstr = shorten_str(copy(tstr, 1, jx - 1));
					}
					else {
						fto = sfto;
					}
					if (wild_cards && (pos("*", tstr) || pos("?", tstr))) {
						tstr2 = get_path(tstr);
						if (!first_file(tstr)) {
							do {
								tstr = tstr2 + last_file_name;
								switch_win_id(ow);
								call do_load_file;
								new_win = window_id;
								if (check_key && ((key1 == 27) && (key2 == 1)) ||
										(load_count >= max_count)) {
									break;
								}
								switch_win_id(tag);
							} while (!next_file);
						}
					}
					else if (file_exists(tstr)) {   // Check for file
						switch_win_id(ow);
						call do_load_file;
						new_win = window_id;
					}
					if (check_key && ((key1 == 27) && (key2 == 1)) ||
							(load_count >= max_count)) {
						break;
					}
					switch_win_id(tag);
					down;
					if ((tstr = shorten_str(get_line)) == "") {
						break;
					}
				}
				delete_window;
				switch_win_id(new_win);
			}                                                // Tmj
			refresh = srf;
		}
	}
// ****************************************************************** Added >>>
	else if (wild_cards && (pos("*", tstr) || pos('?', tstr))) {        // ldh
		if(  parse_int('/NHA=', mparm_str) == 0  ) {
			RM('USERIN^HISTORY_LIST /M=1/HISTORY=FILE_HISTORY');
		}
		TStr2 = Get_Path(TStr);
		if(  FIRST_FILE(TSTR) == 0  ) {
			TStr = TStr2 + LAST_FILE_NAME;
			Call Do_Load_File;
			while(  (NEXT_FILE == 0) & (Load_Count < max_count)  ) {
				TStr = TStr2 + LAST_FILE_NAME;
				Call Do_Load_File;
				if (check_key && ((key1 == 27) && (key2 == 1))) {             // ldh
					break;                                                      // ldh
				}                                                             // ldh
			}
		}
	} else {
/* Support for default file extensions T.R. */
		if(  (Parse_Int('/DE=',MParm_Str))  ) {
			if ( !XPos(".",Truncate_Path(Tstr),1) && (File_Exists(Tstr) != 1) ) {
				Tstr2 = Global_Str('@DEFAULT_EXT_LIST');
				if(  (Svl(Tstr2))  ) {
/* Parse out each extension one at a time until a file is found with that
extension, or we come to the end of the list. */
					Jy = 0;
MORE_EXTS:
					if(  (jy > Svl(Tstr2))  ) {
/* If we come to the end of the list, use no extension, unless the global integer
points to a desired default, and consider it a new file. */
						Mstr = '';
						Tr = Global_Int('@DEFAULT_EXT_POS');
						while(  (Tr)  ) {
							Call PARSE_EXT;
							--Tr;
						}
						Goto GOT_EXT;
					}
					Call PARSE_EXT;

					Return_Str = Tstr + Mstr;
					RM('SETFILENAME');
					if(  (File_Exists(Return_Str) == 1)  ) {
						Goto GOT_EXT;
					}

					Goto MORE_EXTS;
GOT_EXT:
					TStr = Tstr + Copy(Mstr,1,4);
				}
			}
		}

NO_EXT:
		return_str = tstr;

		RM('SetFileName');
		tstr = return_str;
		if(  parse_int('/NHA=', mparm_str) == 0  ) {
			RM('USERIN^HISTORY_LIST /M=1/HISTORY=FILE_HISTORY');
		}
		Call DO_LOAD_FILE;
	}

	Set_Global_Int('LOAD_COUNT',Load_Count);
	return_int = load_count;

	GOTO Exit;

DO_LOAD_FILE:
	tstr = caps(FExpand(TStr));
	t_win = cur_window;
	tr = refresh;
	refresh = false;
	if(  load_check  ) {
		if(  switch_file( tstr )  ) {
			RM( 'USERIN^VERIFY /S=1/H=FL/T=' + tstr + ' already loaded, load again?');
			if(  return_int <= 0  ) {
				++Load_Count;
//        window_attr = window_attr & $40;
				if (window_attr & $40)
					RM("WINDOW^ZOOM");
				window_attr = 0;
				refresh = tr;
				good_load = False;
				ret;
			}
			switch_window( t_win );
		}
	}

	if(  (Load_Count + lc2) > 0  ) {
		Error_Level = 0;
		if(  Window_Count >= 128  ) {
			Goto NO_WINDOWS;
		}
		if(  load_count > 0  ) {
			refresh = FALSE;
		} else {
			refresh = TRUE;
		}
		RM('CreateWindow');
		Redraw;
		if(  Error_Level != 0  ) {
no_windows:
			Load_Count = 20000;
			Error_Level = 0;
			Make_Message('OUT OF WINDOWS!!!');
			good_load = False;
			RET;
		}
	}

	if(  load_count == 0  ) {
		refresh = tr;
	}
	Make_Message('Loading "' + TStr + '"...');
	RM('EXTSETUP /PRE=1/EXT=' + get_extension( tstr ) + "/FTO=" + str(fto));  /*   03-15-91 01:06pm  */
	if(  (Load_Count > 0) | (parse_int('/XL=', mparm_str)) ) {
		XLoad_File(Tstr);
		xload = TRUE;
	} else {
		Load_File(TStr);
		xload = FALSE;
	}

	if(  Error_Level == 3002  ) {
		mstr = '';
		Error_Level = 0;
		RETURN_INT = 0;
		if(  global_str('@FNF_LOAD_MACRO@') != ''  ) {
			RM( global_str('@FNF_LOAD_MACRO@') + ' /F=' + tstr );
		}
		if(  return_int == 0  ) {
			if(  (error_level == 0) && (CAPS(file_name) == '?NO-FILE?') ) {
				MStr = TStr + ' NOT Found,  NEW file assumed.';
				File_Name = FExpand(TStr);
			} else {
				Error_level = 3002;
				Mstr = 'Error:'+Str(Error_Level)+' occured loading "' + TStr + '"';
			}
		}
	} else if(  error_level != 0  ) {
		erase_window;
		RM('MEERROR^ERRMSG');
		Mstr = '"' + TStr + '":  Error ' + str(error_level) + ' ' + return_str;
	} else { /* IF (Load_Count > 0) or (EOF_IN_MEM > 0) or (xload) THEN  */
		if(  Read_Only  ) {
			TStr = ' (Read-Only).';
		} else {
			TStr = '.';
		}
		Mstr = '"' + File_Name + '" loaded' + tstr;
	}
	if(  mstr != ''  ) {
		Make_Message(MStr);
	}
	File_Changed = False;
	if(  error_level == 0  ) {
		RM( 'EXTSETUP /POST=1/PRE=' + parse_str('/PRE=',mparm_str) + '/FTO=' + str(fto));
	}
	++Load_Count;
	good_load = True;
	RET;

PARSE_EXT:
	jx = XPos( ';', Tstr2, jy + 1 );
	if(  jx == 0  ) {
		jx = Svl(Tstr2) + 1;
	}
	Mstr = Copy( Tstr2, jy + 1, jx - jy - 1 );
	if(  (Str_Char(Mstr,1) != '.')  ) {
		Mstr = '.' + Mstr;
	}
	jy = jx;
	RET;

EXIT:
test2:
	file_search_rec = t_search_rec;
	File_Search_Attr = T_File_Search_Attr;
	RM('SetWindowNames');

}  // ldFiles

macro AUTOSAVE TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: AUTOSAVE

Description:  Checks all windows and saves all modified files.  Can be invoked
							by the autosave timer system, or as part of the exit macro.

Parameters:
							/EX=1  tells autosave to clean up autosave files and save all
										 changed files if the global autosave_ext was defined.

							/EX=2  tells autosave to clean up autosave files and exit
										 if the global autosave_ext was defined.

	if the global autosave_ext is not defined the parameter /EX has no effect

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int Save_Count,JX,OLD_WIN, t_error, b1, old_refresh;
/* Start of .ASV modification block 1
	Def_Str(f_name, flg_name, ASV_name, save_ext);
	def_int (t_Ex, tmp_backup);

	t_Ex := Parse_Int('/EX=', mparm_str);
End of .ASV modification block 1  */

	old_refresh = refresh;
	Refresh = False;
	OLD_WIN = CUR_WINDOW;
	Save_Count = 0;
	t_error = 0;
	b1 = 0;
	JX = 1;
	while(  jx <= Window_Count  ) {
		Switch_Window(jx);
		++jx;

/* Start of .ASV modification block 2
		save_ext := '.' + global_str( 'autosave_ext' );
		flg_name   := Truncate_Extension(Truncate_Path(File_Name)) + save_ext;
		IF (t_Ex) THEN
			IF (Global_Int(flg_name)) THEN
				if (backups = 2) Then
					ASV_Name := Backup_Path +
											Truncate_Extension(Truncate_Path(File_Name)) +
											save_ext;
				else
					ASV_Name := Truncate_Extension(File_name) +
											save_ext;
				end;
				Put_Box( 20, 5, 60, 8, 0, m_s_color, 'REMOVING AUTOSAVE FILES...', TRUE );
				Draw_Char( 32, 22, 6, m_t_color, 35 );
				Write( truncate_path( ASV_name ), 26, 6, 0, m_t_color );
				Set_File_Attr(ASV_name,0);
				del_file(ASV_name);           /* delete autosave file if exiting */
				File_changed := TRUE;
			END;
			save_ext := '.';
		END;
		IF ((t_Ex = 2) and (global_str( 'autosave_ext' ) <> '')) THEN
			goto ASV_quit;
		END;
End of .ASV modification block 2  */

		if(  (File_Changed != 0) && (((window_attr & $80) == 0) ||
			(caps(file_name) != "?NO-FILE?"))  ) {
			if(  NOT(b1)  ) {
				b1 = TRUE;
				Put_Box( 20, 5, 60, 8, 0, m_s_color, 'SAVING FILES...', TRUE );
			}
			Draw_Char( 32, 22, 6, m_t_color, 35 );
			Write( truncate_path( file_name ), 26, 6, 0, m_t_color );
			if(  CAPS(File_Name) == '?NO-FILE?'  ) {
				Refresh = True;
				Redraw;
				RM( 'MEUTIL1^SAVEFILE' );
				if(  (Return_Int == 0)  ) {
					T_Error = 1;
				}
				Refresh = False;
			} else {
				rm("MEUTIL1^SAVEFILE /NP=1/NM=1/NCO=1");
/* Start of .ASV modification block 3
				IF (save_ext <> '.') THEN
					f_name := File_Name;
					if (backups = 2) Then
						File_Name := Backup_Path +
												Truncate_Extension(Truncate_Path(File_Name)) +
												save_ext;
					else
						File_Name := Truncate_Extension(File_name) +
												save_ext;
					end;
					Set_Global_Int(flg_name,1);
					if (CAPS(TRUNCATE_PATH(File_Name)) <> '!BUF.0') and
						(CAPS(TRUNCATe_PATH(File_Name)) <> 'MEFIND.TMP') then
						tmp_backup := backups;
						Backups := 0;
						SAVE_FILE;
						Backups := tmp_backup;
					end;
					File_Name := f_name;
					File_Changed := 0;
				ELSE
					SAVE_FILE;
				END;
End of .ASV modification block 3 */
			}
			if(  Error_Level != 0  ) {
				RM( 'MEERROR' );
				t_error = true;
			}
			++Save_Count;
		}
/*
ASV_quit:
 */
	}
	if(  b1  ) {
		kill_box;
	}
	Switch_Window(OLD_WIN);
	Refresh = old_refresh;
	return_int = t_error;
}

macro AUTOSAVETIMER TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: AUTOSAVETIMER

Description:  Manages autosave timer events.  The timer can be based on the
amount of time that has passed since the last time the user pressed a key (or moved
the mouse), or on an absolute interval.  This macro is only intended to be run
by assigning it as a timer event macro.  EXAMPLE:
Timer_Handle := SET_TIMER_EVENT('AUTOSAVETIMER /NOW=0/T=10/S=0', 18, EDIT);

Parameters:
						/NOW= If <> 0, will immediately autosave.  This is for the absolute
						interval method.  IF 0, then the keyboard inactivity method is used.
						/S= If <> 0, then the complete status of the editor is saved as
						well as the files.


							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	if(  (Global_Int('Menu_Level'))  ) {
/* We don't want to autosave in the middle of a menu or prompt */
		Goto EXIT;
	}
	if(  parse_int('/NOW=', mparm_str)  ) {
		goto GO_AUTOSAVE;
	}
	if(  global_int('!LAST_AUTOSAVE_TIME') < last_keypress_time  ) {
		if(  (last_event_time - last_keypress_time) >= parse_int('/T=', mparm_str)  ) {
GO_AUTOSAVE:
			set_global_int( '!LAST_AUTOSAVE_TIME', last_event_time );
			SET_GLOBAL_INT('@TEMP_RETURN_INT', return_int );
			SET_GLOBAL_STR('@TEMP_RETURN_STR', return_str );
			RM('AUTOSAVE');
			if(  (Parse_Int('/S=',MParm_Str))  ) {
				RM('EXIT^STATUS');
			}
			return_int = Global_Int('@TEMP_RETURN_INT');
			return_str = Global_Str('@TEMP_RETURN_STR');
			set_global_int('@TEMP_RETURN_INT',0);
			set_global_str('@TEMP_RETURN_STR','');
			update_status_line;
		}
	}
EXIT:
}

macro SETAUTOSAVE TRANS {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SETAUTOSAVE

Description:  Creates 1 or 2 autosave events from parameters passed by the
installation and setup screen.

Parameters:
						/H1= Existing timer event handle.  This macro will release this
								 handle before assigning a new one to replace it.  The new
								 handles are saved into GLOBAL_STR( '!AUTOSAVEPARMS') using
								 identical parameter syntax.  You must pass these handles to
								 this macro, or they will remain in force.
						/H2= Same as above.

						/T1= Timeout factor for event 1 in seconds.
						/T2= Timeout factor for event 2.

						/M1= Timeout method for event 1.
								0=Keyboard inactivity method.
								1=Absolute interval method.
						/M2= Timeout method for event 2.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  h1,h2,T1,T2,M1,M2,S1,S2;
	str  astr[80] ;

	astr = Global_str('!AUTOSAVEPARMS');
	h1 = parse_int('/H1=', astr );
	h2 = parse_int('/H2=', astr );
	m1 = 0;
	m2 = 0;
	S1 = parse_int('/S1=', mparm_str );
	S2 = parse_int('/S2=', mparm_str );
	if(  h1 != 0  ) {
		kill_timer_event( h1 );
		h1 = 0;
	}
	if(  h2 != 0  ) {
		kill_timer_event( h2 );
		h2 = 0;
	}
	T1 = parse_int('/T1=', mparm_str ) * 18;
	T2 = parse_int('/T2=', mparm_str ) * 18;
	if(  T1 > 0  ) {
		M1 = parse_int('/M1=', mparm_str );
		if(  M1 == 0  ) {
			h1 = set_timer_event('AUTOSAVETIMER /T=' + str( T1 ) + '/S=' + Str(S1), 28, EDIT);
		} else if(  M1 == 1  ) {
			h1 = set_timer_event('AUTOSAVETIMER /NOW=1/S=' + Str(S1), T1, edit);
		}
	}
	if(  T2 > 0  ) {
		M2 = parse_int('/M2=', mparm_str );
		if(  M2 == 0  ) {
			h2 = set_timer_event('AUTOSAVETIMER /T=' + str( T2 ) + '/S=' + Str(S2), 28, EDIT);
		} else if(  M2 == 1  ) {
			h2 = set_timer_event('AUTOSAVETIMER /NOW=1/S=' + Str(S2), T2, edit);
		}
	}

	Set_Global_STR( '!AUTOSAVEPARMS', '/H1=' + str(H1) + '/H2=' + str(H2) +
									'/M1=' + str(M1) + '/M2=' + str(M2) +
									'/T1=' + str(T1 / 18) + '/T2=' + str(T2 / 18) +
									'/S1=' + str(S1) + '/S2=' + str(S2)
									);

}

macro SetWindowNames {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SetWindowNames

Description:  Goes through the entire set of windows currently loaded
		and assigns alphabetic WINDOW_NAMEs to them, skipping any with the
		INVISIBLE (Bit 7) attribute.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  tw, jx, jy,jz, t_refresh,
		error_win_id = Global_Int('~MEERR_ID') ;

	t_refresh = refresh;
	refresh = FALSE;
	tw = cur_window;

	jx = 0;
	jy = 0;
	jz = 0;
	while(  jx < window_count  ) {
		++jx;
		switch_window(jx);
		if(  (window_attr & $80) == 0  ) {
			if( window_id == error_win_id )
			{
				window_name = 'OUTPUT';
			}
			else
			{
				++jy;
				if(  jy > 26  ) {
					jy = 1;
					++jz;
				}
				if(  jz > 0  ) {
					window_name = CHAR( jz + 48) + CHAR( jy + 64 );
				} else {
					window_name = CHAR( jy + 64 );
				}
			}
		}
	}

	switch_window( tw );
	refresh = t_refresh;

}

macro KEYMAC_LOAD {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: KEYMAC_LOAD

Description:  Loads, saves, erases keystroke macros from KEYMAC.DB

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Active_Window,
					Keymac_Window,
					Keymac_Count,
					T_Reg_Exp_Stat = Reg_Exp_Stat,
					T_Insert_Mode = Insert_Mode,
					T_Mode,
					T_Refresh,
					Load_Mode,
					K_int,
					Keep_Window;

	T_Refresh = Refresh;
	Refresh = False;
	Load_Mode = Parse_Int('/LM=',MParm_Str);
	Keep_Window = Parse_Int('/KW=',MParm_Str);
	Keymac_Count = 0;
	Reg_Exp_Stat = True;
	Active_Window = Window_Id;
	RM('Setconfig /DB=MECONFIG/T=KEYMAC.DB');

	if(  (Return_Int == 0)  ) {
		Goto NOT_FOUND;
	} else if(  (Search_Fwd('%@*@*@*@*START@*@*@*@*$',0) == False)  ) {
NOT_FOUND:
		if(  (Parse_Int('/NE=',MParm_Str))  ) {
			Error_Level = 0;
			Goto EXIT;
		}
		if(  (Load_Mode == 1)  ) {
			RM('MEERROR /EM=Can''t find ' + Return_Str + ' to save this keystroke macro in.');
		} else if(  (Load_mode == 2)  ) {
			RM('MEERROR /EM=Can''t find ' + Return_Str + ' to erase keystroke macros.');
		} else {
			RM('MEERROR /EM=Can''t find ' + Return_Str + ' to load keystroke macros.');
		}
		Goto EXIT;
	}
	if(  (Load_Mode == 1)  ) {
		reg_Exp_Stat = TRUE;
		RM('KEYMAC^DOUBLE_127');
		if(  (Search_Fwd('%|12',0))  ) {
			Up;
		} else {
			Eof;
			if(  (C_Col == 1)  ) {
				Up;
			}
		}
		Eol;
		Insert_Mode = True;
		Cr;
		Put_Line('D=' + Parse_Str('/D=',MParm_Str) + 'K=' +
			Parse_Str('/K=',MParm_Str) + 'M=' + Parse_Str('/M=',MParm_Str) +
			'S=' + Convert_String(Global_Str('@KEYBUF')));
		Set_Global_Str('@KEYBUF','');
	} else {
/* Find each keystroke macro record and set up the global and key assignment */
		Down;
		Goto_Col(1);
		while(  ((Not(At_Eof)) & (Cur_Char != '|12'))  ) {
			if(  (Get_Line != '')  ) {
				T_Mode = Parse_Int('M=',Get_Line);
				if(  ((T_Mode != 0) & (T_Mode != 1) & (T_Mode != 3) &
						(T_Mode != 255))  ) {
/* Make sure the ME mode is a legal value */
					T_Mode = 0;
				}
				K_Int = Parse_Int('K=',Get_Line);
				if(  (K_Int)  ) {
/* Only load those macros who have a key assigned to them */
					++keymac_count;
					if(  (Load_Mode == 2)  ) {
/* This will unload the macros from memory */
						set_global_str('@KM!#' + str(keymac_count),'');
						Unassign_key(k_int,t_mode);
					} else {
						set_global_str('@KM!#' + str(keymac_count),Reconvert_String(Parse_str('S=',Get_Line)));
						global_to_key(K_Int, '@KM!#' + str(keymac_count),T_Mode);
					}
				}
			}
			Down;
		}
		if(  (Load_Mode == 2)  ) {
			set_global_int('@KEYMACRO_COUNT@',0);
/* This will reload the keymap in case a keystroke macro had previously
overwritten a keymap assignment */
			ERROR_LEVEL = 0;
			RM(User_Id+'INIT^'+User_Id+'KEYMAP');
			if(  ERROR_LEVEL != 0  ) {
				RM('INIT^KEYMAP');
			}
		} else {
			set_global_int('@KEYMACRO_COUNT@', keymac_count );
		}
	}

EXIT:
/*
Refresh := True;
Window_Attr := 0;
Eof;
Redraw;
Read_Key;
 */
/*
	IF (Window_Created) THEN
		IF (Keep_Window) THEN
			Window_Attr := $81;
		ELSE
			Delete_Window;
		END;
	END;
 */
	Switch_Win_id(Active_Window);
	Reg_Exp_Stat = T_Reg_Exp_Stat;
	Insert_Mode = T_Insert_Mode;
	Refresh = T_Refresh;
}

macro PARMS1 DUMP {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: PARMS1

Description:  Called by Multi-Edit internally to process most command line
		parameters.  Gets called before PARMLOAD, after INIT and before STARTUP.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int Parm_Number;
	int jx, default_back, kspeed ;
	str TStr;
	char Tchar;


	set_global_int("@NO_MOUSE_SET", TRUE );

  return_str = Global_Str('~TEMP_PATH' );
	rm('XlateCmdLine');
	temp_path = return_str;
  if(  ((Length(Temp_Path) > 0) &
      (Copy(Temp_Path,Length(Temp_Path),1) != '\'))  ) {
    Temp_Path = Temp_Path + '\';
  }

	return_str = backup_path;
  return_str = Global_Str('~BACKUP_PATH');
	rm('XlateCmdLine');
	backup_path = return_str;
  if(  ((Length(Backup_Path) > 0) &
      (Copy(Backup_Path,Length(Backup_Path),1) != '\'))  ) {
    Backup_Path = Backup_Path + '\';
  }


	kspeed = (Global_Int('NO_KEYSPEED') == 0);

		 /* Do we need to set default colors? */
	if(  global_int('DEFAULT_COLORS')  ) {
		intr($11);
		if(  (r_ax & $0030) == $0030  ) {
			call set_mono;
		} else {
			call set_color;
		}
	}

	Parm_Number = 1;
	while(  Parm_Number <= PARAM_COUNT  ) {
		TStr = Caps(PARAM_STR(Parm_Number));
		if(  XPOS(Copy(TStr,1,1),'/-',1) != 0  ) {
			TStr = Copy(TStr,2,80);
			TChar = Copy(Tstr,1,1);
			if(  (Length(TSTR) > 1) & (XPos(Copy(Tstr,2,1),'0123456789',1) != 0)  ) {
				if(  VAL(jx, Copy(Tstr,2,8)) == 0  ) {
				}
			} else {
				if(  (Length(tstr) == 2) & (XPOS(Copy(Tstr,2,1), 'YN', 1) != 0)  ) {
					if(  (Copy(Tstr,2,1) == 'Y')  ) {
						jx = true;
					} else {
						jx = false;
					}

					if(  TChar == 'K'  ) {
						Set_Global_Int('NO_KEYSPEED', jx == 0 );
						kspeed = jx;
					}
					if(  TChar == 'N'  ) {
						Name_Line = jx;
					}
					if(  TChar == 'L'  ) {
						Logo_Screen = jx;
					}
				} else {
					if(  (Length(TStr) == 1)  ) {
/* This is a list of command line options that occurs if a /? is used on the
command line. */
						if(  (Tchar == '?')  ) {
							Rest_Dos_Screen;
							Write_Sod('The following command line options are available:|13|10');
							Write_Sod('  /Ln = Go to line n in first file loaded.|13|10');
							Write_Sod('  /MN = Mouse disable, /MY = Mouse enable|13|10');
              Write_Sod('  /MF = Force fully compatible mouse mode|13|10');
							Write_Sod('  /KN = Don''t change keyspeed, /KY = Change keyspeed|13|10');
							Write_Sod('  /NR = No Restore;  Does not change the setup|13|10');
							Write_Sod('  /B  = Use black and white colors, /C = Use default colors|13|10');
							Write_Sod('  /Xn = Sets screen width to n.|13|10');
							Write_Sod('  /Yn = Sets screen height to n.|13|10');
							Write_Sod('  /O  = OS/2 switch on.|13|10');
							Write_Sod('  /EN = Don''t try to use enhanced keyboard|13|10');
							Write_Sod('  /D  = Use direct video.  Bypass Desq-View/Windows|13|10');
							Write_Sod('  /V  = Don''t wait for retrace on CGA video I/O.|13|10');
              Write_Sod('  /Rmacro_name = Run the macro: macro_name.|13|10');
							Write_Sod('  /EMSOFF = Do NOT use expanded/extended memory.|13|10');
							Write_Sod('  /BIGVM = Use large virtual memory mode.|13|10');
              Write_Sod('  /@filename = Load file list.|13|10');
// new options (and old) - loading of files [with filetype override]
							Write_Sod('  filename [filename [...]] = Load the specified file.|13|10');
							Write_Sod('      Multiple files may be loaded, and optionally, use|13|10');
							Write_Sod('      the following File Type Override specifiers:|13|10');
							Write_Sod('  /!filename = Load file in UNIX format.|13|10');
							Write_Sod('  /&filename = Load file in Binary format.|13|10');
							Write_Sod('  /*filename = Load file in MSDOS text format.|13|10');
							quit(0);
						}
						if(  TChar == 'B'  ) {
							/* /B force monochrome defaults */
							Set_Global_Int('COM_DEFAULT_COLORS',1);
							call set_mono;
						}
						if(  TChar == 'C'  ) {
							Set_Global_Int('COM_DEFAULT_COLORS',1);
							call set_color;
						}
					}
				}
			}
		Next_While:
		}
		Parm_Number = Parm_Number + 1;
	}

	if(  kspeed  ) {
		R_BX = (Global_Int('KEYDELAY') << 8) | GLOBAL_INT('KEYSPEED');
		R_AX = $0305;
		INTR( $16 );
		Set_Global_Int('NO_KEYSPEED', 0 );
	}
	goto exit;

color_defaults:
	stat1_color = lightgray | default_back;
	stat2_color = white | default_back;
	message_color = lightgray | default_back;
	fnum_color = lightgray | default_back;
	fkey_color = 112;
	w_t_color = LightGray | default_back;
	w_s_color = yellow | default_back;
	w_b_color = lightgray | default_back;
	w_h_color = 112;
	w_eof_color = white | default_back;
	w_l_color = yellow | default_back;
	w_lb_color = 113;
	w_c_color = white | default_back;
	m_t_color = LightGray | default_back;
	m_s_color = white | default_back;
	m_b_color = white | default_back;
	m_k_color = cyan | default_back;
	m_h_color = 112;
	d_t_color = m_t_color;
	d_s_color = m_s_color;
	d_b_color = m_b_color;
	d_h_color = m_h_color;
	h_t_color = lightgray | default_back;
	h_s_color = white | default_back;
	h_r_color = yellow | default_back;
	h_b_color = lightgray | default_back;
	h_h_color = 112;
	h_f_color = 112;
	h_t1_color = 33;
	h_t2_color = 41;
	h_t3_color = 240;
	working_color = 240;
	background_color = default_back | white;
	Error_color = white | default_back;
	Shadow_Color = LightGray | default_back;
	Shadow_Char = Char(177);
	button_color = 7;
	button_key_color = 15;
	button_shadow_color = default_back | lightgray;
	Set_Global_Str('@SYNTAX_COLORS','/RWC=271/SYC=271/ECC=880/SCC=265/C1C=880/C2C=880/NCC=0');
	ret;

set_color:
	Error_Color = 79;
	Shadow_Color = 8;
	Shadow_Char = '|0';
	W_T_Color = 23;
	W_H_Color = 96;
	W_B_Color = 27;
	W_C_Color = 31;
	w_l_color = 30;
	w_lb_color = 110;
	W_EOF_Color = 20;
	W_S_Color = 30;
	M_T_Color = 112;
	M_S_Color = 113;
	M_B_Color = 112;
	M_H_Color = 95;
	m_k_color = 120;
	CB_H_Color = 63;
	CB_T_Color = 48;
	CB_S_Color = 49;
	Button_Color = 27;
	Button_Key_Color = 30;
	Button_Shadow_Color = 120;
	D_T_Color = 112;
	D_S_Color = 113;
	D_B_Color = 112;
	D_H_Color = 80;
	H_T_Color = 23;
	H_T1_Color = 27;
	H_T2_Color = 28;
	H_T3_Color = 127;
	H_S_Color = 31;
	H_B_Color = 23;
	H_H_Color = 112;
	H_R_Color = 30;
	H_F_Color = 112;
	FKey_Color = 112;
	FNum_Color = 49;
	Stat1_Color = 112;
	Stat2_Color = 113;
	Message_Color = 112;
	Working_Color = 192;
	Background_Color = 23;
	Set_Global_Str('@SYNTAX_COLORS','/RWC=283/SYC=267/ECC=316/SCC=298/C1C=268/C2C=268/NCC=266');
	ret;

set_mono:
	CB_H_Color = 112;
	CB_T_Color = 7;
	CB_S_Color = 15;
	default_back = 0;
	Call Color_Defaults;
	Set_Global_Str('@SYNTAX_COLORS','/RWC=271/SYC=271/ECC=880/SCC=265/C1C=880/C2C=880');
	ret;

exit:
}

macro MouType TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: MouType

Description:  Sets the mouse cursor visually.

Parameters:   /C=ascii      This is the ascii code for the character to be
															used for display.
														0 = use original character on screen.
							/M=mode       0 = Invert attributes on screen.
														1 = Use current screen attribute.
														2 = Set specific attribute
							/A=attr       Attribute to use for /M=2

***************(C) Copyright 1991 by American Cybernetics, Inc.****************/

	int
					c = parse_int('/C=', mparm_str),
					m = parse_int('/M=', mparm_str),
					a = parse_int('/A=', mparm_str),
					ca,
					aa,
					ab = $FF00
					;
	if(  mou_installed  ) {

		if(  c == 0  ) {
			ca = $FF;
		} else {
			ca = 0;
		}
		if(  m == 0  ) {
			aa = $7700;
		} else if(  m == 1  ) {
			aa = 0;
		} else if(  m == 2  ) {
			ab = $0;
			aa = a << 8;
		}
		R_AX = $000A;
		R_BX = 0;
		R_CX = ab | CA;
		R_DX = aa | c;
		INTR( $33 );
	}
}



/****************** Multi-Edit INTEGER Macro Function ***********************

 NAME:         ld_override_file

 DESCRIPTION:  loads a filename from command-line using
	 using a filetype override specifier

	command line syntax:   me /<specifier>filename ...

	multiple type-overridden filenames are valid.

				! - unix format override
				& - binary format override
				* - msdos text format override

 PARAMETERS:  str tstr	filename with type specifier
	      int lc	current value of Load_Count in PARMLOAD
	      		macro or 0 to just load the file.

 RETURNS:     return_int as returned by LDFILES macro (new Load_Count)

*****************************12-02-93 01:35am*******************************/

int ld_override_file(str fstr, int lc)
{
  str tstr = fstr; // str_char requires local string variable!!!

  int this_fto = 0;
  int use_def_ext = 0;


  switch ( str_char( tstr, 1) )
  {
    case '!':	// load as unix file
      this_fto = 2;
      tstr = str_del(tstr, 1, 1);
      break;
    case '&': // load as binary file
      this_fto = 3;
      tstr = str_del(tstr, 1, 1);
      break;
    case '*': // load as msdos text file
      this_fto = 1;
      tstr = str_del(tstr, 1, 1);
      break;
    default:
      use_def_ext = 1;
      break;
  }
  return_str = tstr;
  RM('LDFILES /DE='+str(use_def_ext)+
  	'/FTO='+str(this_fto)+
  	'/LC=' + str(lc));
  return(return_int);		// return new Load_Count
}


macro PARMLOAD DUMP {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: PARMLOAD

Description:  This macro is called by Multi-Edit directly after STARTUP.  It
							takes care of loading in files and executing macros from the
							command line.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

#ifdef DEMO
// special stuff for the demo to support Evolve
	if (global_int('!EVgDialectId')) {
/*
		beep;
		make_message("!EVgDialectId=" + str(global_int('!EVgDialectId')));
		read_key;
*/
		RM("EVINIT");
	}
#endif // DEMO

	int Load_Count,Parm_Number;
	int jx, tw, tr, mouse_initialized = FALSE ;
	int restore_enable = 1, no_restore = 0, Goto_L_Num = 0;
	str TStr, TStr2, Goto_F_Name[80] ;
	set_global_int('MENU_LEVEL', global_int('MENU_LEVEL') + 1 );
	make_message('Welcome to Multi-Edit V' + Version + '.  Press ' + global_str('!HM_KEY1') + ' for help.');
	refresh = false;
	working;
	RM('.STARTUP^STARTUP');

	refresh = false;
	working;

/* Check and see if MECONFIG is read_only.  If so then prompt the
user, to see if he/she wants them unlocked */
	tw = window_id;
	if(  user_id == ''  ) {
		tstr = me_path + 'MECONFIG';
	} else {
		// 7.00.0: tstr = me_path + user_id + '.USR\MECONFIG';
		// 7.00.a: consult the USER_PATH for user directory
		return_str = '<USER_PATH>MECONFIG';
		rm('XlateCmdLine');
		tstr = return_str;
	}

/* This provides support for a default DB file extension to be configured.  It
is overridden if the calling macro specifies an extension */
	if(  (Global_Str('@DB_EXTENSION') == '')  ) {
		Tstr = Tstr + '.DB';
	} else {
		Tstr = Tstr + '.' + Global_Str('@DB_EXTENSION');
	}
	if(  switch_file( tstr )  ) {
		if(  read_only  ) {
			call init_mouse;
			RM('MEERROR^MessageBox /NW=1/T=WARNING!!/M=Your configuration files are locked.  Before unlocking them, insure that there are NOT multiple copies of Multi-Edit running with your user id.');
			RM('USERIN^XMENU /B=1/X=25/Y=8/T=1/L=ACTION:/M=Unlock configuration files()Ignore and leave files locked()');
			kill_box;
			if(  return_int == 1  ) {
				tr = refresh;
				refresh = false;
				make_message('Unlocking configuration files...');
				working;
				delete_window;
				if(  user_id == ''  ) {
					if(  (Global_Str('@DB_EXTENSION') == '')  ) {
						tstr = me_path + '*.DB';
						call unlock_files;
					} else {
						tstr = me_path + '*.' + Global_Str('@DB_EXTENSION');
						call unlock_files;
					}
					tstr = me_path + '*.ME';
					call unlock_files;
				} else {
					tstr = me_path + user_id + '.USR\*.*';
					call unlock_files;
				}
				tstr = user_id + 'MEERR.TMP';
				call unlock_files;
				tstr = me_path + user_id + '*.TMP';
				call unlock_files;
				goto config_unlocked;

				unlock_files:
					jx = FIRST_FILE(TSTR);
					while(  jx == 0  ) {
						Set_File_Attr( get_path( tstr ) + last_file_name,
													 last_file_attr & $FE );
						jx = next_file;
					}
					ret;

	config_unlocked:
				make_message('Configuration files unlocked.');
				refresh = tr;
			}
		}
	}
	switch_win_id( tw );


	error_level = 0;

	Error_Level = 0;

	load_count = 0;

	Parm_Number = 1;
	refresh = false;
	while(  Parm_Number <= PARAM_COUNT  ) {
		TStr = PARAM_STR(Parm_Number);
		TStr = Caps(Remove_Space(TStr));

		if(  Xpos(Copy(Tstr,1,1),'/-',1)  ) {
			Tstr = Copy(Tstr,2,80);

			if ( copy(tstr,1,1) == "@" )
			{
						// Load files from a file list
				refresh = false;
				restore_enable = False;
				Set_Global_Int('LOAD_COUNT',Load_Count);
				int tid = window_id;
				create_window;
				window_attr = 0x81;
				tstr = copy(tstr,2,128);
				load_file( tstr );
				int tag_win = window_id;
				make_message("");
				if(error_level)
				{
					call init_mouse;
					rm("messagebox /B=2/T=ERROR/M=List file " + tstr + " not found.");
				}
				else
				{
					switch_win_id( tid );
					RM('LDFILES /TAG=' + str(tag_win) + '/DE=1/CW=2/LC=' + str(load_count));
					Load_Count = return_int;
					if(  (Load_Count == 1)  ) {
	/* Save the first file loaded so that if the user specifies a line number to
	go to, we can do it. */
						Goto_F_Name = File_Name;
					}
					tid = window_id;
				}
				refresh = false;
        if( switch_win_id( tag_win ))
          delete_window;
				switch_win_id(tid);
			}
			else if(  TStr == 'NR'  ) {
				restore_enable = False;
				no_restore = TRUE;
			} else if(  (TStr == 'NS') ) {		// no restore(windows), no save status
				restore_enable = False;
				no_restore = TRUE;
				set_global_int('RESTORE',0);
			} else if(  (TStr == 'R')  ) {
				call init_mouse;
				++Parm_Number;
				restore_enable = False;
				Refresh = True;
				if(  PARAM_Str(Parm_Number) != ''  ) {
					RM(PARAM_Str(Parm_Number));
				}
      } else if (copy(tstr,1,1) == "R") {
        call init_mouse;
				restore_enable = False;
        refresh = true;
        tstr = copy(tstr,2,40);
        if (tstr != "") {
          RM(tstr);
        }
			} else if(  (Str_Char(TStr,1) == 'L')  ) {
/* This is the go to line number parameter */
				if(  (Val(Goto_L_Num,Copy(TStr,2,10)) == 0)  ) {
					if(  ((Goto_L_Num < 0) | (Goto_L_Num > 2000000000))  ) {
						Goto_L_Num = 0;
					}
				}
			}
		// ----------------------------------------------------------------------
		// mods to handle loading unix,msdos  and binary file type overrides

		// to load a file in unix mode (to override or it is really is):
		// 	C:\> me /!unix.fil

		// to load a file in binary mode
		// 	C:\> me /&binary.fil

		// to load a file in msdos text mode
		// 	C:\> me /*msdostxt.fil

		// or load multiple files
		// 	C:\> me normal.fil /!unix.fil /&binary.fil /*msdostxt.fil

		// 			Yuuuck!

		  else if ( Xpos(Copy(Tstr,1,1),'!&*',1) )
		  {
		    refresh = false;
		    restore_enable = False;
		    Set_Global_Int('LOAD_COUNT',Load_Count);

		    // Load file as unix, msdos, or binary format file
		    Load_Count = ld_override_file(tstr, Load_Count);
		    if(  (Load_Count == 1)  ) {
		      // Save the first file loaded so that if the user
		      // specifies a line number to go to, we can do it.
		      Goto_F_Name = File_Name;
		    }
		    refresh = false;
		  }
		}
		else {
			if(  (Load_Count < 20000) & (Pos(copy(TStr,1,1),'/-') == 0) & (copy(TStr,1,1) != ';')  ) {
				if(  load_count == 0  ) {
					refresh = true;
				}
				restore_enable = False;
				Set_Global_Int('LOAD_COUNT',Load_Count);
				return_str = tstr;
				RM('LDFILES /DE=1/LC=' + str(load_count));
				Load_Count = return_int;
				if(  (Load_Count == 1)  ) {
/* Save the first file loaded so that if the user specifies a line number to
go to, we can do it. */
					Goto_F_Name = File_Name;
				}
			}
		}
		++Parm_Number;
	}

/* Load keystroke macros if present */
	RM('KEYMAC_LOAD /NE=1');
	Error_Level = 0;

	if(  (Goto_L_Num)  ) {
/* Go directly to specified line number */
		if(  (Switch_File(Goto_F_Name))  ) {
			Goto_Line(Goto_L_Num);
		}
	}
	if(Global_Int('RESTORE')) {
		if (Restore_Enable) {
			RM( 'EXIT^RESTORE' );
			Error_Level = 0;
		}
		else if( !no_restore )
		{
			RM( 'EXIT^RESTORE /M=1' );
			Error_Level = 0;
		}
	}


/* This is for the "post restore" startup macro which can be used to do stuff
that you don't want restore to undo like setting globals */
	if(  (Length(Global_Str('@STARTUP_MAC_2')))  ) {
		RM(Global_Str('@STARTUP_MAC_2'));
		Error_Level = 0;
	}
	goto exit;

init_mouse:

	if( !mouse_initialized )
			rm("INIT_MOUSE");
	set_global_int("@NO_MOUSE_SET", 0 );
	mouse_initialized = TRUE;
	ret;

EXIT:
	set_virtual_display;
	refresh = true;
	if(  load_count > 0  ) {
		switch_window(1);
	}
exit2:
	if(  window_name == ''  ) {
		RM('SetWindowNames');
	}
	tstr = global_str('!AUTOSAVEPARMS');
	if(  (parse_int('/T1=', tstr ) != 0) | (parse_int('/T2=', tstr ) != 0)  ) {
		RM('SetAutoSave ' + tstr);
	}
	set_global_int('MENU_LEVEL', 0);
	update_virtual_display;
	reset_virtual_display;
	call init_mouse;
}

macro SPACE {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SPACE

Description:  This macro simply puts a space character at the current cursor
							position.  It is intended to be used with <CtrlSpace>,
							<CtrlShiftSpace> and <AltShiftSpace> in the keymap.


							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	Text(" ");
}