/*******************************************************************************
														MULTI-EDIT MACRO FILE

Name:   DIRSHELL

Description:  The Multi-Edit File Manager (formerly DOS Shell)

DIRSHELL - The main File Manager interface
DIR_SHELL_EVENT
INITDIRSHELL
UPDATEDIR
DIRWINDOW
PROCESSDIR
DIRSORT
DIROPERATION
RUNDOSCMD
UPDATEDRIVES
DIRMARKREPEAT
COPYMARKEDFILES
DELMARKEDFILES
PRINTMARKEDFILES
DIRMOVE - Enables mouse movement in the File Manager
MARKLOAD - Loads marked files in the File Manager
ARCDIR - Creates a list of files contained in an arced or zipped file
DIRFILEVIEW
TREE
FILEATTR

							 (C) Copyright 1991 by American Cybernetics, Inc.
********************************************************************************/

macro DIRSHELL FROM EDIT TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: DIRSHELL

Description:	This implements the user interface to the Multi-Edit File Manager.

Parameters:		/S=nn		Select type, where:
												0 = Normal File Manager
												1 = Invoked from the Load File prompt.
															Should not load any files, just return
															a file name in Return_Str.
												2 = Single directory, limited use.

							All parameters below have defaults.
							/X= Upper left corner X coordinate of directory list.
							/Y= Upper left corner Y coordinate of directory list.
							/W= Width of directory list.
							/L= Length of directory list.
							/D= Directory mask.  Defaults to *.*.

Returns:
							Return_Int  If 1, load file was selected.
							Return_Str  The last highlited directory entry

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str  tstr[150], ev_string[20], drive_str[32], mask[12] ;
	int  jx, Select_Type, j1,j2, wx, wy, cur_dir, old_refresh ;
	int  x, y, l, w, ev_count, dd_row ;
	int  old_x, old_y, dir_result ;
	int  t_d_t_color, t_d_b_color, t_d_h_color, t_d_s_color, t_fkey_row ;
	int  min_dir_row, max_dir_row, t_mode ;

	char  cur_drive ;

	ev_string = '@DSLEV';
	t_mode = mode;
	mode = dos_shell;
	dir_result = 0;
	old_refresh = refresh;
	refresh = true;
	cur_dir = 1;
	wx = wherex;  wy = wherey;  /* get the cursor position */
	Return_Str = '';
	Select_Type = Parse_Int('/S=', MParm_Str);
	dir_search_attr = $37;
	t_fkey_row = fkey_row;
	RM('InitDirShell /S=' + str(select_type));
	tstr = global_str('@DIR_PARMS@');
	max_dir_row = parse_int('/MAXR=',tstr);
	min_dir_row = parse_int('/MINR=',tstr);
	drive_str = parse_str('/DS=', tstr );
	Push_Labels;
		/* this is for a single directory list */
	if(  select_type == 2  ) {
		t_d_s_color = d_s_color;
		t_d_h_color = d_h_color;
		t_d_t_color = d_t_color;
		t_d_b_color = d_b_color;

		d_b_color = m_b_color;
		d_s_color = m_s_color;
		d_h_color = m_h_color;
		d_t_color = m_t_color;

		cur_dir = 5;
		set_global_int('@DIR_CUR_DIR@', cur_dir);
		x = parse_int('/X=', mparm_str);
		y = parse_int('/Y=', mparm_str);
		w = parse_int('/W=', mparm_str);
		l = parse_int('/L=', mparm_str);

		if(  y < min_dir_row  ) {
			y = min_dir_row;
		}
		if(  y > (max_dir_row - 10)  ) {
			y = max_dir_row - 10;
		}

		if(  x < 1  ) {
			x = 1;
		}
		if(  x > (screen_width - 28)  ) {
			x = screen_width - 28;
		}
		if(  (y + l) > (max_dir_row)  ) {
			l = (max_dir_row - y) + 1;
		}
		if(  (x + w + 2) >= (screen_width)  ) {
			w = (screen_width - x) - 1;
		}

		Tstr = 'Help  ChDir DeleteCopy  RenameDir OfCMD   Print SelectSort              MrkDelMrkCpyMrkRepView  AttribMrkPrt      Tree  ';

		Jx = 1;
		while(  (Jx <= 20)  ) {
			FLABEL(Copy(Tstr,((Jx - 1) * 6) + 1,6),Jx,-1);
			++ Jx;
		}

		open_dir(5);
		switch_dir(5);
		size_dir(x,y,x + w - 1, y + l - 1, 1);
		Dir_Sort_Str = Global_Str('DIR_SORT_STR');

		tstr = parse_str('/D=', mparm_str );
		call process_dir_string;
		call Update_Cur_Dir;
	} else {
		x = 1;
		y = min_dir_row;
		cur_dir = global_int('@DIR_CUR_DIR@');
		if(  cur_dir == 0  ) {
			cur_dir = 1;
		}
		if(  cur_dir > 4  ) {
			cur_dir = 1;
		}
		set_global_int('@DIR_CUR_DIR@', cur_dir);
		if(  dir_active(cur_dir)  ) {
			switch_dir(cur_dir);
		} else {
			Open_Dir( cur_dir );
			Dir_Mode = Global_Int('@DIR_MODE@');
		}

	/* Assign labels for File Manager */
		Tstr = 'Help  ChDir DeleteCopy  RenameDir OfCMD   Print Load  Sort  NxtDirSizDirMrkDelMrkCpyMrkRepView  AttribMrkPrt      Tree  ';

		Jx = 1;
		while(  (Jx < 21)  ) {
			FLABEL(Copy(Tstr,((Jx - 1) * 6) + 1,6),Jx,-1);
			++ Jx;
		}

		FLABEL('Open',31,-1);
		FLABEL('Close',32,-1);

		if(  (Select_Type != 1)  ) {
			FLABEL('MrkLd',19,-1);
		}
		FLABEL('Record',40,-1);

		if(  Select_Type == 1  ) {
			Make_Message( 'Select file and press <ENTER>, or <ESC> to exit without selecting.' );
		}

	/* Instructions for right of directory list */

		Dir_Sort_Str = Global_Str('DIR_SORT_STR');
		Call Update_All_Dirs;
	}
	Dir_Sort_Str = Global_Str('DIR_SORT_STR');
	Update_Status_Line;
main_loop:
	cur_dir = global_int('@DIR_CUR_DIR@');
	if(  error_level != 0  ) {
		RM('MEERROR');
		error_level = 0;
	}
	refresh = TRUE;
	read_key;
	if(  (key1 == 0)  ) {
		if(  (select_type != 2)  ) {
			if(  (key2 == 84)  ) {  /* Switch dir */
				RM('DirWindow /M=0');
				goto main_loop;

			} else if(  (key2 == 104)  ) {  /* Create dir */
				RM('DirWindow /M=1');
				goto main_loop;

				/* Delete dir */
			} else if(  (key2 == 105)  ) {
				RM('DirWindow /M=2');
				goto main_loop;

			}
		}

		if(  (key2 == 59)  ) {
			help('DIRSHELL^*');
			goto main_loop;

		} else if(  (key2 == 238) | (key2 == 85)  ) {
			RM('DirWindow /M=3');
			goto main_loop;

		} else if(  (key2 == 60)  ) {  /* change directory */
			RM('DirOperation /M=1');
			goto main_loop;

		} else if(  (key2 == 61) | (key2 == 83)  ) { /* delete file */
			RM('DirOperation /M=3');
			goto main_loop;

		} else if(  (key2 == 62)  ) { /* copy file */
			RM('DirOperation /M=2');
			goto main_loop;

		} else if(  (key2 == 63)  ) { /* Rename file */
			RM('DirOperation /M=4');
			goto main_loop;

		} else if(  (key2 == 64)  ) {  /* change directory mask */
			RM('DirOperation /M=0');
			goto main_loop;

		} else if(  (key2 == 65)  ) {  /* dos command */
			call strip_boxes;
			RM( 'MEUTIL1^SHELLDOS' );
			call update_all_Dirs;
			goto Main_Loop;

		} else if(  (key2 == 66)  ) {  /* print command */
			rm('CheckPSInstalled');
			if ( !Return_Int )
				Goto MAIN_LOOP;

			return_str = 'PRINT ' + Dir_Entry;
			RM('RunDosCmd');
			Goto Main_Loop;
		}

		if(  (key2 == 67)  ) {  /* load file or look at directory */
Load_Dir_Entry:
			if(  (Dos_File_Attr & $10) != 0  ) {
				tstr = FExpand(Dir_Entry);
				if(  Copy(tstr,length(tstr), 1) != '\'  ) {
					tstr = tstr + '\';
				}
				Return_Str = get_path( dir_mask );
				Dir( tstr + truncate_path( dir_mask ) );
				CALL find_old_entry;
				call redraw_cur_dir;
				Goto Main_Loop;
			}

			tstr = Caps(Get_Extension(Dir_Entry));
			if(  (Tstr == 'EXE') | (TStr == 'COM') | (TStr == 'BAT')  ) {
				if(  (Tstr == 'BAT')  ) {
					Create_Global_Str('DIRBAT_1','/H=DIRSHELL^*');
					Create_Global_Str('XDIRBAT_1','Load batch file');
					Create_Global_Str('DIRBAT_2','/H=DIRSHELL^*');
					Create_Global_Str('XDIRBAT_2','Run batch file');

					RM('USERIN^TOPMENU /M=XDIRBAT_/G=DIRBAT_/#=2/S=1/BC=1/GCLR=1/L=' + Truncate_Path(Dir_Entry));
					if(  (Return_Int == 1)  ) {
						Goto BAT_LOAD;
					} else if(  (Return_Int < 1)  ) {
						Goto MAIN_LOOP;
					}
				}
				return_str = Dir_Entry;
				RM('Exec /S=1');
				call redraw_all_dirs;
				goto main_loop;
			}
			if(  (tstr == 'ARC') | (tstr == 'ZIP') | (tstr == 'LZH') ) {
				RM('ArcDir ' + dir_entry);
				call redraw_cur_dir;
				goto main_loop;
			}

BAT_LOAD:
			if(  Select_Type == 0  ) {
				tstr = Dir_Entry;
				Call Dos_Exit;
				dir_result = True;
				return_str = tstr;
				RM( 'LDFILES /CW=2' );
				Redraw;
				goto Exit;
			}

			if(  (Select_TYpe == 1) | (select_type == 2)  ) {
				dir_result = TRUE;
				Goto Exit;
			}
		}

		if(  (key2 == 68)  ) { /* sort */
			RM('DIRSORT');
			if(  return_int > 0  ) {
				call update_cur_dir;
			} else {
				call redraw_cur_dir;
			}
			goto main_loop;
		}

		if(  (key2 == 86)  ) { /* delete marked files */
			RM('userin^VERIFY /L=2/C=2/H=DIRSHELL^DODELMARKED/T=Are you sure you want to delete these files?');
			if(  Return_Int  ) {
				RM('DelMarkedFiles');
				if(  select_type != 2  ) {
					Make_Message('Marked files deleted.');
				}
				dd_row = RETURN_INT;
			}
      goto main_loop;
		}

		if(  (key2 == 87)  ) { /* copy marked files */
			return_str = '';
			RM('USERIN^QUERYBOX /L=' + str( y ) + '/C=' + str(x) +
					'/W=60/H=DIRSHELL^DOCOPYMARKED/T=Please enter destination');
			if(  Return_Int  ) {
				RM('CopyMarkedFiles');
				if(  select_type != 2  ) {
					Make_Message('Marked files copied.');
				}
				dd_row = RETURN_INT;
			}
			goto main_loop;
		}
		if(  (key2 == 88)  ) { /* repeat command on marked files */
			return_str = '';
			RM('USERIN^QUERYBOX /L=' + str( y ) + '/C=' + str(x) +
					'/W=60/H=DIRSHELL^MARKEDREPEAT/T=Please enter command line');
			if(  Return_Int  ) {
				RM('DirMarkRepeat');
			}
			dd_row = RETURN_INT;
			goto main_loop;
		}
		if(  (key2 == 89)  ) { /* view file */
			return_str = dir_entry;
			RM('DirFileView');
			goto main_loop;
		}
		if(  (key2 == 90)  ) { /* view file attributes */
			return_str = dir_entry;
			RM('Fileattr');
			if(  (Return_Int)  ) {
				call update_cur_dir;
			} else {
				call redraw_cur_dir;
			}
			goto main_loop;
		}

		if(  (key2 == 91)  ) { /* print marked files */
			rm('CheckPSInstalled');
			if ( !Return_Int )
				Goto MAIN_LOOP;

			RM('userin^VERIFY /L=2/C=2/H=DIRSHELL^DOPRINTMARKED/T=Are you sure you want to print these files?');
			if(  Return_Int  ) {
				RM('PrintMarkedFiles');
				if(  select_type != 2  ) {
					Make_Message('Marked files printed.');
				}
			}
			goto main_loop;
		}

		if(  (key2 == 92)  ) { /* load marked files */
			if(  Select_Type == 0  ) {
				call strip_boxes;
				RM('MARKLOAD');
				Goto Exit;
			} else {
				if(  select_type != 2  ) {
					Make_Message('NOT Available from the Load_File prompt.');
				}
				Goto Main_Loop;
			}
		}
		if(  (key2 == 93)  ) {
			RM('TREE /CP=' + get_path(dir_mask) + '/CD=' + get_path(dir_mask));
			if(  return_int  ) {
				return_str = return_str + truncate_path(dir_mask);
				RM('ProcessDir');
			}
			RM('UpdateDir /M=0');
			goto main_loop;
		}

		if(  (select_type != 2) & (key2 == 113)  ) { /*  start/stop recording of keystroke macro  */
			key_record;
			goto Main_Loop;
		}
		if(  (Key2 == 245)  ) {
			goto do_escape;
		}
		if(  (Key2 == 244)  ) {
			goto do_enter;
		}
		if(  (Key2 == 246)  ) {
      key2 = 251;
     // goto do_mark;                            // TMJ
		}
  //  if(  (key2 == 251)  ) {                    // TMJ now handled in DIR_SHELL_EVENT
  //    CALL MOUSE_EVENT2;
  //    GOTO MAIN_LOOP;
 //   }
		if(  (key2 == 250)  ) {  /* mouse event */
			CALL MOUSE_EVENT1;
			if(  (Return_Int)  ) {
				Goto Load_Dir_Entry;
			}
			GOTO main_loop;
		}
    RM('DIR_SHELL_EVENT /TAG=1/FM=1/DDR=' + str(dd_row));       //TMJ
		dd_row = global_int('!DIR_SHELL_DD_ROW' );

	} else {
//    if(  (key1 == 32)  ) {
//do_mark:
//      Mark_File;
//      goto main_loop;
//    } else
    if(  (key1 == 27)  ) {
do_escape:
			Return_Str = '';
			Goto Exit;
		} else if(  (key1 == 13) & ((key2 == 28) | (key2 == 224))  ) {
do_enter:
			Goto Load_Dir_Entry;
		} else if ((key1 == 9) && (key2 == 15)) {
			dir_mode = not(dir_mode);
/* We will probably want to get rid of this line when we get an install screen
for the File Manager */
			Set_Global_Int('@DIR_MODE@',Dir_Mode);
			call redraw_cur_dir;
			goto main_loop;
		} else {
      RM('DIR_SHELL_EVENT /TAG=1/FM=1/DDR=' + str(dd_row));  //TMJ
			dd_row = global_int('!DIR_SHELL_DD_ROW' );
		}
	}

	jx = Inq_Key(Key1,Key2,DOS_SHELL, tstr);
	if(  (jx == 1) | (jx == 2)  ) {
		Pass_Key( key1, key2 );
/*    RM(TStr);  */
	}
	goto main_loop;

Find_Old_Entry:
	refresh = false;
	dos_home;
	if(  copy( return_str, length(return_str), 1) == '\'  ) {
		return_str = copy( return_str, 1, length( return_str ) - 1 );
	}
	while(  dir_num < dir_total  ) {
		if(  dir_entry == return_str  ) {
			REFRESH = TRUE;
			RET;
		}
		DOS_RIGHT;
	}
	dos_home;
	REFRESH = TRUE;
	RET;

/********************************** SUBROUTINES ********************************/

		int  jy ;

redraw_all_dirs:
		RM('UpdateDir /M=4' );
		dd_row = return_int;
		RET;

XRedraw_Cur_Dir:
	RM('UpdateDir /M=1');
	dd_row = return_int;
	ret;

Redraw_Cur_Dir:
	RM('UpdateDir /M=0');
	dd_row = return_int;
	ret;

Update_Cur_Dir:
	Dir( dir_mask );
	call redraw_cur_dir;
	ret;

XUpdate_Cur_Dir:
	Dir( dir_mask );
	call xredraw_cur_dir;
	ret;

lo_cur_dir:
	RM('UpdateDir /M=2');
	dd_row = return_int;
	ret;
	RET;

process_dir_string:
		return_str = tstr;
		RM('ProcessDir');
		tstr = get_path( dir_mask );
		mask = truncate_path( dir_mask );
		ret;

MOUSE_EVENT1:
		old_x = Mou_Last_X;
		old_y = Mou_Last_Y;
		ev_count = parse_int('/EVC=', global_str('@DIR_PARMS@'));
		RM('UserIn^CheckEvents /M=1/G=' + ev_string + '/#=' + str( ev_count ));
		if(  return_int != 0  ) {
			return_int = parse_int('/R=', return_str );
			if(  return_int == 0  ) {
				push_key( 27, 1 );
			} else if(  return_int == 1  ) {
				push_key( 13, 28 );
			}
		} else if(  (Mou_Last_Y == Fkey_Row)  ) {
			RM( 'MOUSE^MouseFkey' );
		} else if(  (Mou_Last_X >= dir_x1) & (Mou_Last_X <= dir_x2) &
				(Mou_Last_Y >= dir_y1) & (Mou_Last_Y <= dir_y2)  ) {
      RM('DIR_SHELL_EVENT /TAG=1/FM=1/DDR=' + str(dd_row));            //TMJ
			dd_row = global_int('!DIR_SHELL_DD_ROW' );
			if(  (Return_Int)  ) {
				Ret;
			}
		} else if(  select_type != 2  ) {
			/* Check to see if the mouse was clicked inside another dos window */
			int  tcd = 0, new_d_s = 0 ;

			jx = 0;
			while(  jx < 4  ) {
				++jx;
				if(  dir_active(jx) & (jx != cur_dir)  ) {
					switch_dir( jx );
					if(  (Mou_Last_X >= dir_x1) & (Mou_Last_X <= dir_x2) &
							(Mou_Last_Y >= dir_y1) & (Mou_Last_Y <= dir_y2) &
							(global_int("~DIR_DRAW#" + str( jx )) >= new_d_s  )) {
						tcd = jx;
						new_d_s = global_int("~DIR_DRAW#" + str( jx ));
					}
				}
			}
			if( tcd > 0 ) {
				switch_dir( cur_dir );
				call lo_cur_dir;
				switch_dir( tcd );
				cur_dir = tcd;
				set_global_int('@DIR_CUR_DIR@', cur_dir);
				call redraw_cur_dir;
			}
			else
				switch_dir( cur_dir );
		}
		Return_Int = 0;
		RET;

MOUSE_EVENT2:
		if(  (Mou_Last_X > dir_x1) & (Mou_Last_X < dir_x2) &
			(Mou_Last_Y > (dir_y1 + 1)) & (mou_last_y < dir_y2)  ) {
			if(  Dir_Locate_Mouse  ) {
				Mark_File;
			}
		}
		RET;


/*******************************************************************************/
update_all_dirs:
	RM('UpdateDir /M=3');
	dd_row = RETURN_INT;
	RET;

strip_boxes:
	jx = parse_int('/BC=', global_str('@DIR_PARMS@'));
	while(  box_count > jx  ) {
		kill_box;
	}
	ret;

Dos_Exit:
		call strip_boxes;
/*
	close_dir( 1 );
	close_dir( 2 );
	close_dir( 3 );
	close_dir( 4 );
 */

	if(  dir_active(5)  ) {
		close_dir(5);
	}
	if(  select_type != 2  ) {
		set_global_int('LAST_DIR', cur_dir);
	} else {
		d_s_color = t_d_s_color;
		d_h_color = t_d_h_color;
		d_t_color = t_d_t_color;
		d_b_color = t_d_b_color;
	}
	pop_labels;
	fkey_row = t_fkey_row;
	kill_box;
	GotoXy(wx,wy);
	mode = t_mode;
	refresh = old_refresh;
	Ret;

Exit:
	ev_count = parse_int('/EVC=', global_str('@DIR_PARMS@'));
	RM('UserIn^CheckEvents /M=3/G=' + ev_string + '/#=' + str( ev_count ));
	Return_Str = Dir_Entry;
	Call Dos_Exit;
	return_int = DIR_RESULT;
	set_global_int('DIR_SHELL_DD_ROW', 0 );
}

macro Dir_Shell_Event TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name: DIR_SHELL_EVENT

Description: Processes certain mouse and keyboard events.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  result = 0,
					j1, jx, jy, old_x, old_y,
					dm = parse_int('/DM=', mparm_str),
					full_mode = parse_int('/FM=', mparm_str),
          allow_tagging = parse_int("/TAG=", mparm_str),
					nd = parse_int('/ND=', mparm_str );
				 ;

	int  dd_row = parse_int('/DDR=', mparm_str) ;
	str  drive_str[32], tstr[128] ;

	if( nd )
			dd_row = dir_y1 - 1;

	drive_str = parse_str('/DS=', global_str('@DIR_PARMS@') );
	if(  (key1 == 0)  ) {
		if(  (key2 == 250)  ) {
			old_x = mou_last_x;
			old_y = mou_last_y;
			if( (Mou_last_y > dir_y1) && (Mou_Last_X < dir_x2) && (Mou_Last_X > dir_x1) &&
				(Mou_Last_Y > (dd_row))  ) {
				jx = dir_num;

				if(  Dir_Locate_Mouse  ) {
					RM('DirMove /V=1/H=' + str(dir_mode) + '/DDR=' + str(dd_row));
					if(  mou_double_click  ) {
						result = 1;
					}
				}
			/*  Check for drive change  */
			} else if( !nd && ( (Mou_Last_Y > dir_y1) & (Mou_Last_Y <= dd_row) ) ) {
				jx = (Mou_Last_Y - dir_y1 - 1) * (( dir_x2 - dir_x1) / 3);
				JX =  (((Mou_Last_X - dir_x1) - 1) / 3) + 1 + jx;
				if(  jx <= svl( drive_str )  ) {
					Return_Str = fexpand( (str_char( drive_str, jx ) + ':' + truncate_path( dir_mask )) );
					Dir( Return_Str );
					call redraw_cur_dir;
				}
			} else if(  (Mou_Last_X == dir_x1) & (Mou_Last_Y == dir_y1)  ) {
				jx = 1;
				call mouse_set_size;
			} else if(  (Mou_Last_X == dir_x2) & (Mou_Last_Y == dir_y1)  ) {
				jx = 2;
				call mouse_set_size;
			} else if(  (Mou_Last_X == dir_x2) & (Mou_Last_Y == dir_y2)  ) {
				jx = 4;
				call mouse_set_size;
			} else if(  (Mou_Last_X == dir_x1) & (Mou_Last_Y == dir_y2)  ) {
				jx = 3;
				call mouse_set_size;
			} else if(  (Mou_Last_X == dir_x2)  ) {
				if(  (Mou_Last_Y == (dir_y2 - 1))  ) {
					refresh = TRUE;
					ll4:
						Dos_Down;
						CALL Mouse_Repeat_Loop;
						if(  return_int == 1  ) {
							Goto ll4;
						}
				} else if(  (Mou_Last_Y == (dd_row + 2))) {
					refresh = TRUE;
					ll1:
					Dos_Up;
					CALL Mouse_Repeat_Loop;
					if(  return_int == 1  ) {
						Goto ll1;
					}
				} else if(  (Mou_Last_Y > (dd_row + 1)) & (Mou_Last_Y < (dir_y2 - 1))  ) {
					jx = Mou_Last_Y - dd_row - 2;
					if(  dir_scroll_pos > jx  ) {
					ll2:
						Call Page_Up;
						CALL Mouse_Repeat_Loop;
						if(  return_int == 1  ) {
							Goto ll2;
						}
					} else if(  dir_scroll_pos < jx  ) {
					ll3:
						Call Page_Dn;
						CALL Mouse_Repeat_Loop;
						if(  return_int == 1  ) {
							Goto ll3;
						}
					} else if(  dir_scroll_pos == jx  ) {
						Mou_Set_Limits(dir_x2,dd_row + 3,dir_x2, dir_y2 - 2);
						du3:
							Mou_Check_Status;
							if(  ((Mou_Last_Status & 1) != 0)  ) {
								if(  old_y != mou_last_y  ) {
									old_y = mou_last_y;
									jy = Mou_Last_Y - dd_row - 2;
									if(  dir_scroll_pos > jy  ) {
										while(  dir_scroll_pos > jy  ) {
											call page_up;
										}
									} else if(  dir_scroll_pos < jy  ) {
										while(  dir_scroll_pos < jy  ) {
											call page_dn;
										}
									}
								}
								goto du3;
							}
						Mou_Set_Limits(1,1,screen_width, screen_length);
					}
				}
			} else if(  (Mou_Last_X > dir_x1) & (Mou_Last_X < dir_x2) &
				(Mou_Last_Y == dir_y1)  ) {
				jx = length( dir_mask );
				jy = (dir_x1 + ((dir_x2 - dir_x1) / 2)) - (jx / 2);
				if(  (Mou_Last_X >= jy) & (Mou_Last_X < (jy + jx))  ) {
					jx = (mou_last_x - jy) + 1;
					tstr = dir_mask;
					while(  (str_char( tstr, jx ) != '\') & (jx <= svl(tstr))  ) {
						++jx;
					}
					Draw_Attr( jy, dir_y1, d_h_color, jx );
					if(  jx < svl(tstr)  ) {
						tstr = copy( tstr, 1, jx ) + truncate_path( tstr );
						return_str = get_path( dir_mask );
						dir( tstr );
						call find_old_entry;
					}
					CALL redraw_cur_dir;
				} else {
					jx = 0;
					call mouse_set_size;
				}
			}
    } else if( key2 == 251 ) {                                       // TMJ
      if(  (Mou_Last_X > dir_x1) & (Mou_Last_X < dir_x2) &         // TMJ
        (Mou_Last_Y > dir_y1) & (mou_last_y < dir_y2)  ) {   // TMJ
        if(  Dir_Locate_Mouse  ) {                                 // TMJ
          if(allow_tagging)                                              // TMJ
          {                                                              // TMJ
            Mark_File;                                               // TMJ
          }                                                          // TMJ
        }                                                            // TMJ
      }                                                              // TMJ
		} else if(  (key2 == 80) | (Key2 == 241)  ) { /* cursor down */
			dos_down;
		} else if(  ((key2 == 72) | (Key2 == 240))  ) { /* cursor up */
			dos_up;
		} else if(  ((key2 == 77) | (key2 == 243))  ) {
			dos_right;
		} else if(  ((key2 == 75) | (key2 == 242))  ) {
			dos_left;
		} else if(  (key2 == 71)  ) { /* home */
			dos_home;
		} else if(  (key2 == 79)  ) { /* end */
			refresh = false;
			while(  dir_num < dir_total  ) {
				dos_down;
				dos_right;
			}
			refresh = true;
			update_dir;
		} else if(  (key2 == 73)  ) { /* Page up */
			Call Page_Up;
		} else if(  (key2 == 81)  ) { /* Page dn */
			Call Page_Dn;
		} else {
			result = -1;
		}
  } else if( key1 == 32 ) {
    if(allow_tagging)
      mark_file;
	} else if(  (key1 == 127)  ) {
		tstr = get_path(dir_mask);
		jx = svl(tstr) - 1;
		while(  (jx > 0) & (str_char(tstr,jx) != '\') &
					(str_char(tstr,jx) != ':')  ) {
			--jx;
		}
		tstr = copy(tstr,1,jx );
		if(  str_char( tstr, jx ) == ':'  ) {
			tstr = tstr + '\';
		}
		return_str = get_path( dir_mask );
		dir( fexpand( tstr + truncate_path( dir_mask ) ) );
		call find_old_entry;
		call redraw_cur_dir;
	} else if(  ((key1 > 0) & (key1 < 27)) &
			NOT((key2 == 14) | (key2 == 28) | (key2 == 224) | (key2 == 15))  ) {
		tstr = char( key1 + 64 );
		if(  xpos( tstr, drive_str, 1 ) > 0  ) {
			Return_Str = fexpand( tstr + ':' + truncate_path( dir_mask ) );
			Dir( Return_Str );
			call redraw_cur_dir;
		}
	} else {
		jx = wherex - dir_x1 - 2;
		if(  dir_mode  ) {
			while(  jx > 14  ) {
				jx = jx - 15;
			}
			return_str = copy(truncate_path(dir_entry), 1, jx );
		} else {
			if(  jx >= 9  ) {
				return_str = truncate_path(truncate_extension(dir_entry)) + '.';
				tstr = get_extension(dir_entry);
				if(  svl(tstr) > 0  ) {
					return_str = return_str + copy(tstr, 1, jx - 9);
				}
			} else {
				return_str = copy(truncate_path(dir_entry), 1, jx );
			}
		}
		if(  key1 == 8  ) {
			if(  return_str != ''  ) {
				return_str = copy(return_str,1, length(return_str) - 1);
			}
		} else {
			return_str = CAPS( return_str + char(key1));
		}
		if(  dir_inc_search( return_str )  ) {
			if(  dir_mode  ) {
				gotoxy( wherex + length(return_str), wherey );
			} else {
				tstr = truncate_extension( return_str );
				if(  length(return_str) > length(tstr)  ) {
					gotoxy( wherex + 9 + length(get_extension(return_str)), wherey );
				} else {
					gotoxy( wherex + length(return_str), wherey );
				}
			}
		}
	}
	goto mouse_exit;

Page_Up:
	refresh = false;
	jx = 1;
	j1 = dir_y2 - dir_y1 - 2;
	while(  (jx < j1)  & (dir_num > 1)  ) {
		++jx;
		dos_up;
	}
	refresh = true;
	update_dir;
	RET;

Page_Dn:
	refresh = false;
	jx = 1;
	j1 = dir_y2 - dir_y1 - 2;
	while(  (jx < j1) & (dir_num < dir_total)  ) {
		++jx;
		dos_down;
	}
	refresh = true;
	update_dir;
	RET;

mouse_set_size:
	if(  full_mode  ) {
		tstr = Global_Str( '@DIR_PARMS@');
		j1 = parse_int('/MAXR=', tstr );
		jy = parse_int('/MINR=', tstr );
		RM('WINDOW^MOVE_WIN /H=6/W=24/X1=' + str(dir_x1)+'/Y1=' +
			Str(dir_y1) + '/X2=' + Str(dir_x2) + '/Y2=' + Str(dir_y2) +
			'/MS=' + str(0) + '/MX1=0/MY1=' + Str(jy) + '/MX2=' +
			Str(Screen_Width + 1) + '/MY2=' + Str(j1) +
			'/MM=' + str( jx ) );
		size_dir( parse_int('/X1=', return_str),
							parse_int('/Y1=', return_str),
							parse_int('/X2=', return_str),
							parse_int('/Y2=', return_str),
							(nd == 0)
						);
		RM('UpdateDir /M=4/ND=' + str( nd ) );
		dd_row = return_int;
		if( nd )
				dd_row = dir_y1;
	}
	RET;

redraw_cur_dir:
	RM('UPDATEDIR /M=' + str( dm ) + '/ND=' + str(nd));
	RET;

Update_Cur_Dir:
	Dir( dir_mask );
	call redraw_cur_dir;
	ret;

Mouse_Repeat_Loop:
		Mou_Repeat = TRUE;
		du2:
			Mou_Check_Status;
			if(  ((Mou_Last_Status & 1) != 0) & (old_x == Mou_Last_X) &
				(old_y == Mou_Last_Y)  ) {
				if(  check_Key  ) {
					if(  (key2 == 250) & (key1 == 0)  ) {
						return_int = 1;
						RET;
					} else {
						push_key( key1, key2 );
					}
				} else {
					GOTO du2;
				}
			}
		return_int = 0;
		Mou_Repeat = FALSE;
		RET;


Find_Old_Entry:
	refresh = false;
	dos_home;
	if(  copy( return_str, length(return_str), 1) == '\'  ) {
		return_str = copy( return_str, 1, length( return_str ) - 1 );
	}
	while(  dir_num < dir_total  ) {
		if(  dir_entry == return_str  ) {
			REFRESH = TRUE;
			RET;
		}
		DOS_RIGHT;
	}
	dos_home;
	REFRESH = TRUE;
	RET;


mouse_exit:
	set_global_int('!DIR_SHELL_DD_ROW', dd_row );
	return_int = RESULT;

}

macro InitDirShell trans2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name: INITDIRSHELL

Description: Initializes certain things for the File Manager.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  drive_count, jx, jy, max_dir_row = screen_length,
					 min_dir_row = 1, t_bc = 0 ;
	str  drive_str[32] ;

	if(  NOT(parse_int('/SMALL=', mparm_str))  ) {
		save_box(1,1,screen_width,screen_length);

		if(  fkey_row == 0  ) {
			fkey_row = screen_length;
		}
		if(  fkey_row == status_row  ) {
			fkey_row = status_row - 1;
		}
		max_dir_row = max_window_row;
		if(  (max_dir_row >= fkey_row)  ) {
			max_dir_row = fkey_row - 1;
		}

		min_dir_row = min_window_row;
		if(  (min_dir_row <= message_row) & (max_window_row > message_row)  ) {
			min_dir_row = message_row + 1;
		}
		if(  (min_dir_row <= status_row) & (max_window_row > status_row)  ) {
			min_dir_row = status_row + 1;
		}
		if(  (min_dir_row <= 0)  ) {
			min_dir_row = 1;
		}
		t_bc = box_count;
	}


/* get active drives on system */
	R_AX = $1900;
	Intr($21);
	jy = R_AX & $00FF;
	R_DX = jy;
	R_AX = $0E00;
	Intr($21);
	jx = 0;
	drive_count = R_AX & $00FF;
	drive_str = '';
	while(  jx < drive_count  ) {
		R_DX = jx;
		R_AX = $0E00;
		Intr($21);
		R_AX = $1900;
		Intr($21);
		if(  (R_AX & $00FF) == jx  ) {
			drive_str = drive_str + char( jx + 65);
		}
		++jx;
	}
	R_DX = jy;
	R_AX = $0E00;
	Intr($21);
	drive_count = svl( drive_str );

	Set_Global_Str( '@DIR_PARMS@', '/MAXR=' + str( max_dir_row )+
											'/BC=' + str(t_bc) +
											'/MINR=' + str(min_dir_row ) +
											'/DS=' + drive_str +
											'/DC=' + str(drive_count)  +
											'/S=' + parse_str('/S=', mparm_str)
									);

}

/********************************MULTI-EDIT MACRO******************************

Name: UpdateDir

Description:  Screen refresh for DIR windows.

Parameters:		/M=n		u_mode
								 0	= Total redraw of current window
								 1  = Partial redraw of current window
								 2  = Lo-light current window
								 3  = Update all windows
								 4  = Redraw all windows
							/TL=1 = No top label.
							/ND=1 = No Drive display

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro UpdateDir trans2 {
	int  u_mode, jx, jy, t_b_color, t_d_color, t_h_color, dd_row, ev_count, cur_dir ;
	int  max_dir_row, min_dir_row, select_type ,nohelp,helpln;
	int redraw_num = global_int("~DIR_DRAW_COUNT" ) + 1;
	str  ev_string[20], tstr[128] ;

	int active_dir = global_int('@DIR_CUR_DIR@');
	working;
	set_virtual_display;
	u_mode = parse_int('/M=', mparm_str);
	nohelp = Parse_int('/NH=',mparm_str);
	if(  u_mode == 0  ) {
		call Redraw_Cur_DIr;
	} else if(  u_mode == 1  ) {
		call XRedraw_Cur_DIr;
	} else if(  u_mode == 2  ) {
		call Lo_Cur_Dir;
	} else if(  u_mode == 3  ) {
		call update_all_dirs;
	} else if(  u_mode == 4  ) {
		call redraw_all_dirs;
	}
	goto exit;

Update_Drive_Display:
		dd_row = 0;
		if( !parse_int('/ND=', mparm_str) ) {
			RM('UpdateDrives');
			dd_row = return_int;
		}
		RET;

XRedraw_Cur_Dir:
	redraw_dir;
	call update_drive_display;
	update_dir;
	set_global_int("~DIR_DRAW#" + str( active_dir ), redraw_num );
	{
		int ttx;
		if( dd_row == 0)
				ttx = dir_y2;
		else
				ttx = dd_row + 1;
		jy = dir_x1 + ((dir_x2 - dir_x1) / 2);
		if(  parse_int('/TL=', mparm_str) == 0  ) {
			tstr = dir_mask;
			if(  svl(tstr) > (dir_x2 - dir_x1)  ) {
				tstr = str_del( tstr,1,2);
				if(  svl(tstr) > (dir_x2 - dir_x1)  ) {
					tstr = truncate_path(tstr);
				}
			}
			write( tstr, jy - (svl( tstr ) / 2),
										dir_y1, 0, d_s_color );
		}
		tstr = str( disk_free( ASCII(copy( dir_mask, 1,1 )) - 64 ) / 1024 );
		write( copy( dir_mask, 1, 2 ) + tstr + 'K', dir_x1 + 1, ttx, 0, d_b_color );
		write( str( dir_total ) + ' files', SVL(tstr) + 5 + dir_x1,
						ttx, 0, d_b_color );
	}
	ret;

Redraw_Cur_Dir:
	CALL XRedraw_Cur_Dir;
	ev_count = 2;
	ev_string = '@DSLEV';
	Set_Global_Str(ev_string + '1', '/T=Select/KC=<ENTER>/K1=13/K2=28/R=1/X='
				+ str(  jy - 11 ) + '/Y=' + str( dir_y2 ) + '/W=13');
	Set_Global_Str(ev_string + '2', '/T=Done/KC=<ESC>/K1=27/K2=1/R=0/X='
				+ str(  jy + 3) + '/Y=' + str( dir_y2 ) + '/W=9');
	RM('UserIn^CheckEvents /M=2/G=' + ev_string + '/#=' + str( ev_count ));
	Return_Str = '/EVC=';
	RM('USERIN^CHNGPARM /G=@DIR_PARMS@/P=' + str(ev_count) );
	RET;

lo_cur_dir:
	t_d_color = d_s_color;
	t_h_color = d_h_color;
	t_b_color = d_b_color;
	d_h_color = d_s_color;
	d_s_color = d_t_color;
	d_b_color = d_t_color;
	call xredraw_cur_dir;
	d_s_color = t_d_color;
	d_h_color = t_h_color;
	d_b_color = t_b_color;
	RET;


Update_Cur_Dir:
	working;
	Dir( dir_mask );
	call redraw_cur_dir;
	ret;

XUpdate_Cur_Dir:
	working;
	Dir( dir_mask );
	call xredraw_cur_dir;
	ret;

set_vars:
	cur_dir = global_int('@DIR_CUR_DIR@');
	select_type = parse_int('/S=', global_str('@DIR_PARMS@'));
	max_dir_row = parse_int('/MAXR=', global_str('@DIR_PARMS@'));
	min_dir_row = parse_int('/MINR=', global_str('@DIR_PARMS@'));
	ret;

update_all_dirs:
	redraw_num = 0;
	working;
	call set_vars;
	call strip_boxes;
	restore_box;
	Call Dos_Help;
	t_h_color = d_h_color;
	d_h_color = d_t_color;
	t_d_color = d_s_color;
	d_s_color = d_t_color;
	jx = 0;
	if(  select_type != 2  ) {
		while(  jx < 4  ) {
			++jx;
			switch_dir(jx);
			if(  dir_active( jx )  ) {
				active_dir = jx;
				if(  (dir_y2 > max_dir_row)  ) {
					size_dir( dir_x1, dir_y1, dir_x2, max_dir_row, 1 );
				}
				if(  (dir_y1 < min_dir_row)  ) {
					size_dir( dir_x1, min_dir_row, dir_x2, dir_y2, 1 );
				}
				if(  (jx != cur_dir)  ) {
					call xupdate_cur_dir;
					++redraw_num;
				}
			}
		}
	} else {
		if(  (dir_y2 > max_dir_row)  ) {
			size_dir( dir_x1, dir_y1, dir_x2, max_dir_row, 1 );
		}
		if(  (dir_y1 < min_dir_row)  ) {
			size_dir( dir_x1, min_dir_row, dir_x2, dir_y2, 1 );
		}
	}
	d_s_color = t_d_color;
	d_h_color = t_h_color;

	switch_dir(cur_dir);
	active_dir = cur_dir;
	call update_cur_dir;
	ret;

strip_boxes:
	jx = parse_int('/BC=', global_str('@DIR_PARMS@'));
	while(  box_count > jx  ) {
		kill_box;
	}
	ret;

redraw_all_dirs:
	redraw_num = 0;
	working;
	call set_vars;
	call strip_boxes;
	restore_box;
	call dos_help;
	t_d_color = d_s_color;
	t_h_color = d_h_color;
	d_h_color = d_t_color;
	d_s_color = d_t_color;
	jx = 0;
	if(  select_type != 2  ) {
		while(  jx < 4  ) {
			++jx;
			if(  (dir_active(jx))  ) {
				switch_dir(jx);
				active_dir = jx;
				if(  (dir_y2) > (max_dir_row)  ) {
					size_dir( dir_x1, dir_y1, dir_x2, max_dir_row, 1 );
				}
				if(  (dir_y1) < (min_dir_row)  ) {
					size_dir( dir_x1, min_dir_row, dir_x2, dir_y2, 1 );
				}
				if(  (jx != cur_dir)  ) {
					call XRedraw_cur_dir;
					++redraw_num;
				}
			}
		}
	}
	d_s_color = t_d_color;
	d_h_color = t_h_color;
	switch_dir(cur_dir);
	active_dir = cur_dir;
	call redraw_cur_dir;
	ret;


dos_help:
		helpln = min_dir_row;
		if(  (select_type != 2) & (Nohelp == False)  ) {
			Put_Box(48,helpln,80,24,0,M_B_Color,'Multi-Edit File Manager',false);
			Write('Use cursor keys to move.',49,++helpln,0,m_t_color);
//			Write('',49,helpln,0,m_t_color);
			Write('<TAB> Toggle display mode',49,++helpln,0,m_t_color);
			Write('<Space> Mark/Unmark file',49,++helpln,0,m_t_color);
			Write('<CtrlBackSpace> Parent dir',49,++helpln,0,m_t_color);
			Write('<F2> Change Directory',49,++helpln,0,m_t_color);
			Write('<F3> Delete File(s)',49,++helpln,0,m_t_color);
			Write('<F4> Copy File(s)',49,++helpln,0,m_t_color);
			Write('<F5> Rename File(s)',49,++helpln,0,m_t_color);
			Write('<F6> Get New Dir Listing',49,++helpln,0,m_t_color);
			Write('<F7> Do any DOS Command',49,++helpln,0,m_t_color);
			Write('<F8> Print file with PRINT.COM',49,++helpln,0,m_t_color);
			Write('<F9> Load File into window',49,++helpln,0,m_t_color);
			Write('<F10> Sort directory',49,++helpln,0,m_t_color);
			Write('<ShftF3> Delete Marked files',49,++helpln,0,m_t_color);
			Write('<ShftF4> Copy Marked files',49,++helpln,0,m_t_color);
			Write('<ShftF8> Print Marked files',49,++helpln,0,m_t_color);
			Write('<ShftF9> Load Marked files',49,++helpln,0,m_t_color);
			Write('<ShftF10> Directory Tree',49,++helpln,0,m_t_color);
			Write('<AltF1> Open directory list',49,++helpln,0,m_t_color);
			Write('<AltF2> Close directory list',49,++helpln,0,m_t_color);
			Write('Press <F1> for more HELP',52,++helpln,0,M_B_color);
		}
		ret;


EXIT:
	set_global_int("~DIR_DRAW_COUNT", redraw_num );
	update_status_line;
	update_virtual_display;
	reset_virtual_display;
	return_int = dd_row;
}

/********************************MULTI-EDIT MACRO******************************

Name: DirWindow

Description:  Performs various directory window operations.

Parameters:		/M=n		w_mode
									= 0 Switch to next dir window
									= 1 Create dir window
									= 2 Delete dir window
									= 3 Resize dir window

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro DirWindow trans2 {
		int  w_mode, jx, cur_dir, select_type, min_dir_row, max_dir_row ;

		cur_dir = global_int('@DIR_CUR_DIR@');
		select_type = parse_int('/S=', global_str('@DIR_PARMS@'));
		max_dir_row = parse_int('/MAXR=', global_str('@DIR_PARMS@'));
		min_dir_row = parse_int('/MINR=', global_str('@DIR_PARMS@'));

		w_mode = parse_int('/M=', mparm_str);
		if(  w_mode == 0  ) {
			call switch_dir;
		} else if(  w_mode == 1  ) {
			call create_dir;
		} else if(  w_mode == 2  ) {
			call delete_dir;
		} else if(  w_mode == 3  ) {
			call resize_dir;
		}
		goto exit;


switch_dir:
		RM('UpdateDir /M=2');
		call switch_dir2;
		RM('UpdateDir /M=0');
		RET;

switch_dir2:
		jx = 1;
switch_again:
		++cur_dir;
		++jx;
		if(  cur_dir > 4  ) {
			cur_dir = 1;
		}
		set_global_int('@DIR_CUR_DIR@', cur_dir);
		switch_dir(cur_dir);
		if(  dir_active( cur_dir ) == 0  ) {
			if(  jx < 5  ) {
				goto switch_again;
			}
		}
		error_level = 0;

		ret;

create_dir:
	jx = 1;
open_again:
	if(  dir_active(jx)  ) {
		++jx;
		if(  jx < 5  ) {
			goto open_again;
		}
	}
	error_level = 0;
	if(  jx < 5  ) {
		RM('UpdateDir /M=2');
		cur_dir = jx;
		set_global_int('@DIR_CUR_DIR@', cur_dir);
		open_dir(cur_dir);
		Dir_Mode = Global_Int('@DIR_MODE@');
		if(  cur_dir == 1  ) {
			size_dir(1,min_dir_row,47,max_dir_row, 1);
		}
		if(  cur_dir == 2  ) {
			size_dir(48,min_dir_row,80,max_dir_row, 1);
		}
		if(  cur_dir == 3  ) {
			size_dir(30,5,65,max_dir_row - 3, 1);
		}
		if(  cur_dir == 4  ) {
			size_dir(15,7,50,max_dir_row - 5, 1);
		}
		Dir_Sort_Str = Global_Str('DIR_SORT_STR');
		Dir( dir_mask );
		RM('UpdateDir /M=0');
	}
	RET;


delete_dir:
	/* Delete dir */
	jx = dir_active(1) + dir_active(2) + dir_active(3) + dir_active(4);
	if(  jx > 1  ) {
		close_dir(cur_dir);
		call switch_dir2;
		RM('UpdateDir /M=4');
	}
	ret;

resize_dir:
	RM(
			'WINDOW^MOVE_WIN /K=1/X1=' + str(dir_x1) +
			'/Y1=' + Str(dir_y1) +
			'/X2=' + Str(dir_x2) +
			'/Y2=' + Str(dir_y2) +
			'/MX1=1/MY1=' + str(min_dir_row ) +
			'/MX2='+str(screen_width) +
			'/MY2='+str(max_dir_row)
		);

	if(  (Return_Int)  ) {
		Size_Dir(Parse_Int('/X1=',Return_Str),Parse_Int('/Y1=',Return_Str),
			Parse_Int('/X2=',Return_Str),Parse_Int('/Y2=',Return_Str), 1);
	}
	RM('UpdateDir /M=4');
	ret;

EXIT:

}

/********************************MULTI-EDIT MACRO******************************

Name: ProcessDir

Description:  Gets a directory of the path and mask stored in RETURN_STR.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro ProcessDir trans2 {
		str dstring[128];
		if(  caps(return_str) == '?NO-FILE?'  ) {
			return_str = '*.*';
		}
		dstring = fexpand(return_str);
		if(  truncate_path(dstring) == ''  ) {
			dstring = dstring + '*.*';
		} else if(  (xpos('.',truncate_path(dstring),1) == 0)  ) {
			dstring = dstring + '.*';
		}
		dir( dstring );
		if(  (dir_total == 1) & ((dos_file_attr & $10) != 0)  ) {
			if(  caps(truncate_path(dir_entry)) == caps(truncate_path(return_str))  ) {
				dir( dir_entry + '\*.*' );
			}
		}

}

/********************************MULTI-EDIT MACRO******************************

Name: DIRSORT

Description:  User interface for setting the directory sort string.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro DIRSORT TRANS {
		int jx;
		set_global_str('DIR_IPARM_1','/TP=10/C=1/T=Extension/QK=1');
		set_global_str('DIR_IPARM_2','/TP=10/C=1/T=Name/QK=1');
		set_global_str('DIR_IPARM_3','/TP=10/C=1/T=Size/QK=1');
		set_global_str('DIR_IPARM_4','/TP=10/C=1/T=Date/QK=1');
		set_global_str('DIR_IPARM_5','/TP=10/C=1/T=Time/QK=1');
		set_global_str('DIR_IPARM_6','/TP=10/C=1/T=-Reverse/QK=1');
		set_global_str('DIR_IPARM_7','/TP=0/T=/C=14/L=1/W=9');
		set_global_str('DIR_ISTR_7', Dir_Sort_Str);
		RM('USERIN^DATA_IN /H=DIRSHELL^DOSORT/#=7/A=2/S=7/T=INPUT SORT KEY/PRE=DIR_');
		if(  return_int  ) {
			Dir_Sort_Str = Global_Str('DIR_ISTR_7');
			Set_Global_Str('DIR_SORT_STR', Dir_Sort_Str );
			var_parse_int( '/UPDATE=', mparm_str, jx );
			if(  jx != 0  ) {
				if(  dir_active(jx)  ) {
					switch_dir(jx);
					if(  dir_total > 0  ) {
						working;
						dir( dir_mask );
						update_status_line;
					}
				}
			}
		}
		set_global_str('DIR_ISTR_7', '');
}

/********************************MULTI-EDIT MACRO******************************

Name:   DirOperation

Description: Dos File Manager file operations:

Parameters:		/M=n		o_mode
									= 0 New dir mask
									= 1 Change directory
									= 2 Copy file
									= 3 Delete file
									= 4 rename file

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro DirOperation TRANS {
		int  o_mode, select_type ;
		str  tstr[128] ;
		select_type = parse_int('/S=', global_str('@DIR_PARMS@'));
		o_mode = parse_int('/M=', mparm_str);

		if(  o_mode == 0  ) {
			call new_dir_mask;
		} else if(  o_mode == 1  ) {
			call change_directory;
		} else if(  o_mode == 2  ) {
			call copy_file;
		} else if(  o_mode == 3  ) {
			call delete_file;
		} else if(  o_mode == 4  ) {
			call rename_file;
		}
		goto exit;

new_dir_mask:
		Return_Str = Dir_Mask;
		if(  select_type != 2  ) {
			Make_Message('Please note that this will NOT actually change the current directory.');
		}
		RM('USERIN^QUERYBOX /L=' + str( dir_y1 + 1 ) + '/C=' + str( dir_x1 + 1) +
			'/W=60/H=DIRSHELL^*/T=Please enter new directory mask');
		if(  select_type != 2  ) {
			Make_Message('');
		}
		if(  Return_Int  ) {
			RM('processdir');
		}
		RM('UpdateDir /M=0');
		ret;
change_directory:
		Return_Str = Dir_Path;
		if(  (Dos_File_Attr & $10) != 0  ) {
			Return_Str = Dir_Entry;
		}
		RM('USERIN^QUERYBOX /L=' + str( dir_y1 ) + '/C=' + str(dir_x1) +
			'/W=60/H=DIRSHELL^DOCD/T=Please enter new directory');
		if(  Return_Int  ) {
			Change_Dir(Return_Str);
			Dir('*.*');
/* If you want to retain the old directory mask when changing directories, use
this line instead of the line above
			Dir( Truncate_Path( dir_mask ));
 */
		}
		RM('UpdateDir /M=0');
		RET;

delete_file:
			Return_Str = Dir_Entry;
			RM('USERIN^QUERYBOX /L=' + str( dir_y1 ) + '/C=' + str(dir_x1) +
				'/W=60/H=DIRSHELL^DODELFILE/T=Please enter file to delete');
			if(  Return_Int  ) {
				Return_Str = 'DEL ' + Return_Str;
				RM('RunDosCmd');
			}
			ret;

copy_file:
			Return_Str = Dir_Entry;
			RM('USERIN^QUERYBOX /L=' + str( dir_y1 ) + '/C=' + str(dir_x1) +
				'/W=60/H=DIRSHELL^DOCOPYFILE/T=Please enter file to copy');
			if(  Return_Int  ) {
				tstr = return_str;
				return_str = '';
				RM('USERIN^QUERYBOX /L=' + str( dir_y1 ) + '/C=' + str(dir_x1) +
						'/W=60/H=DIRSHELL^DOCOPYFILE/T=Please enter destination');
				if(  Return_Int  ) {
					return_str = 'COPY ' + Tstr + ' ' + Return_Str;
					RM('RunDosCmd');
				}
			}
			ret;

rename_file:
			Return_Str = Dir_Entry;
			RM('USERIN^QUERYBOX /L=' + str( dir_y1 ) + '/C=' + str(dir_x1) +
					'/W=60/H=DIRSHELL^DORENAME/T=Please enter file to rename');
			if(  Return_Int  ) {
				tstr = return_str;
				return_str = '';
				RM('USERIN^QUERYBOX /L=' + str( dir_y1 ) + '/C=' + str(dir_x1) +
						'/W=60/H=DIRSHELL^DORENAME/T=Please enter new file name');
				if(  Return_Int  ) {
					return_str = 'RENAME ' + Tstr + ' ' + Return_Str;
					RM('RunDosCmd');
				}
			}
			ret;
EXIT:
}


/********************************MULTI-EDIT MACRO******************************

Name: RunDosCmd

Description:  Runs the dos command specified in RETURN_STR using
							COMMAND.COM.  Use under DIRSHELL ONLY.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro RunDosCmd TRANS {
	int  j1,j2 ;
	rm('EXEC /CMD=1/SCREEN=3/WAIT=1/CMDLN=1');
	RM( 'UpdateDir /M=3' );

}


macro UpdateDrives trans2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name: UPDATEDRIVES

Description: Updates the drive display.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
		int  redx, redc, um ;
		int  jy, dd_row, drive_count, x1, y1, x2, y2 ;
		char  cur_drive ;
		str  drive_str[32] ;

		um = parse_int('/M=', mparm_str);
		if(  um  ) {
			x1 = parse_int('/X1=', mparm_str);
			y1 = parse_int('/Y1=', mparm_str);
			x2 = parse_int('/X2=', mparm_str);
			y2 = parse_int('/Y2=', mparm_str);
			cur_drive = parse_str('/CD=', mparm_str);
		} else {
			x1 = dir_x1;
			x2 = dir_x2;
			y1 = dir_y1;
			y2 = dir_y2;
			cur_drive = copy(dir_mask,1,1);
		}
		drive_count = parse_int('/DC=', global_str('@DIR_PARMS@'));
		drive_str = parse_str('/DS=', global_str('@DIR_PARMS@'));
		redx = 0;
		redc = x1 + 1;
		dd_row = y1 + 1;
		jy = x2 - 1;
		while(  (redx < Drive_Count) & (dd_row < y2)  ) {
			++redx;
			if(  str_char( drive_str , redx ) == cur_drive  ) {
				write( str_char(drive_str,redx ) + ':', redc, dd_row, 0, d_h_color );
			} else {
				write( str_char(drive_str,redx ) + ':', redc, dd_row, 0, d_s_color );
			}
			redc = redc + 3;
			if(  redc >= jy  ) {
				if(  redx < drive_count  ) {
					++dd_row;
					redc = x1 + 1;
				}
			}
		}

		if(  um == 0  ) {
			Size_Dir( x1, y1, x2, y2, (dd_row - y1) + 1 );
		}
		draw_char( 196, x1 + 1, dd_row + 1, d_b_color, x2 - x1 - 1 );
		return_int = dd_row;
}

macro DirMarkRepeat TRANS {
/*******************************************************************************
															MULTI-EDIT MACRO

Name: DIRMARKREPEAT

Description: Repeats the DOS command defined in RETURN_STR for all marked files

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  jx, j1, j2 ;
	str  tstr ;

	tstr = return_str;

	Refresh = False;
	DOS_HOME;
	jx = 0;
	gotoxy_vp(1,1);
	put_box( 2, 6, 79, 20, 0, m_b_color, '', TRUE );
	set_vp( 3, 7, 76, 18 );
	text_color_vp = m_s_color;
	dos_win_x1 = 3;
	dos_win_x2 = 76;
	dos_win_y1 = 7;
	dos_win_y2 = 18;
	dos_output_window = 1;
	while(  Dir_Num != jx  ) {
		jx = dir_num;
		if(  File_Marked  ) {
			return_str = tstr;
			RM('XLATECMDLINE /F=' + dir_entry);
			Write_Vp('>' + return_str + '|13|10');
			Shell_To_DOS(return_str,true);
		}
		Dos_right;
	}
	Write('Press any key to continue...', 28, 19, 0, button_color );
	Read_Key;
	kill_box;
	set_vp( 1,1, screen_width, screen_length );
	dos_output_window = 0;
	Refresh = True;
	RM('UpdateDir /M=3');

}

/********************************MULTI-EDIT MACRO******************************

Name: CopyMarkedFiles

Description:  Copies all marked files in the current dir window to the
		destination stored in RETURN_STR.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro CopyMarkedFiles TRANS {

	return_str = 'COPY <FILE>.<EXT> ' + return_str;
	RM( 'DirMarkRepeat' );

}

macro DelMarkedFiles TRANS {
/********************************MULTI-EDIT MACRO******************************

Name: DELMarkedFiles

Description:  Deletes all marked files in the current dir window

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  jx;
	Refresh = False;

	DOS_HOME;
	jx = 0;
	put_box( 2, 6, 79, 20, 0, m_b_color, '', TRUE );
	set_vp( 3, 7, 76, 18 );
	text_color_vp = m_s_color;
	gotoxy_vp(1,1);
	while(  Dir_Num != jx  ) {
		jx = dir_num;
		if(  File_Marked  ) {
			Write_Vp('>DEL ' + Dir_Entry + '|13|10');
			Del_File(Dir_Entry);
			if(  Error_Level != 0  ) {
				Write_Vp('->Error on ' + Dir_Entry);
				Error_Level = 0;
			}
		}
		Dos_right;
	}
	Write('Press any key to continue...', 28, 19, 0, button_color );
	if ( (key1 == 32) && (Key2 == 57) ) 
	{
		key1 = 13;
		key2 = 28;
	}
	Read_Key;
	kill_box;
	set_vp( 1,1, screen_width, screen_length );
	Refresh = True;
	RM('UpdateDir /M=3');

}

macro PrintMarkedFiles TRANS {
/********************************MULTI-EDIT MACRO******************************

Name: PRINTMarkedFiles

Description:  Prints all marked files in the current dir window

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	rm('CheckPSInstalled');
	if ( !Return_Int )
		Goto Exit;

	return_str = 'PRINT <FILE>.<EXT>';
	rm("DIRMARKREPEAT");
exit:
}

macro DIRMOVE FROM DOS_SHELL TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:   DIRMOVE

Description:  Enables mouse movement in the File Manager.

Parameters:
							/H= If 1, enables horizontal mouse movement.
							/V= If 1, enables vertical mouse movement.

							 (C) Copyright 1991 by American Cybernetics, Inc.
********************************************************************************/

	int  jx, vertical, horizontal, old_x, xoffs, interval = 0,
			 dd_row = parse_int('/DDR=', mparm_str),
			 ox = mou_last_x, oy = mou_last_y ;

	vertical = parse_int('/V=', mparm_str);
	horizontal = parse_int('/H=', mparm_str);
	Mou_Check_Status;
	mou_reset;
	xoffs = old_x - wherex;
loop:
	Mou_Check_Status;
	if(  ((Mou_Last_Status & 3) == 0)  ) {
		return_int = 1;
		GOTO exit;
	}
	if(  (mou_last_x > dir_x1) & (mou_last_x < dir_x2) &
				(mou_last_y < dir_y2) & (mou_last_y > dir_y1) && (mou_last_y > (dd_row + 1))  ) {
		if(( mou_last_x != ox) || (mou_last_y != oy)) {
			dir_locate_mouse;
		}
	} else {
		if( interval > 0 ) {
			if( interval > 3 )
				interval = 3;
			switch ( interval )
			{
				case 1 : delay( 35 );
				case 2 : delay( 15 );
				case 3 : delay( 5 );
			}
			interval = 0;
		}
		if( vertical && (mou_last_y >= dir_y2 )) {
			dos_down;
			interval = mou_last_y - dir_y2 + 1;
		}
		if( vertical && ((mou_last_y <= dir_y1 ) || (mou_last_y <= (dd_row + 1)))) {
			dos_up;
			if( dd_row != 0 ) {
				interval = (dd_row + 1) - mou_last_y + 1;
			}
			else
				interval = dir_y1 - mou_last_y + 1;
		}
	}
	ox = mou_last_x;
	oy = mou_last_y;
	GOTO loop;

exit:
}


macro MARKLOAD FROM DOS_SHELL TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:   MARKLOAD

Description:  From the File Manager, loads all marked files into new windows.

							 (C) Copyright 1991 by American Cybernetics, Inc.
********************************************************************************/

	int jx,tcount;

/*  Go to the top of the DIR listing  */
	Working;
	Refresh = False;
	Dos_Home;

/* Now scan down the entire DIR list, building a global string for
each Marked entry */
	jx = 0;
	tcount = 0;
	while(  jx < Dir_Total  ) {
		JX = JX + 1;
		if(  File_Marked  ) {
			++tcount;
			Set_Global_Str('ML!!' + Str(tcount),Dir_Entry);
		}
		DOS_RIGHT;
	}

/*  close_dir( 1 );
	close_dir( 2 );
	close_dir( 3 );
	close_dir( 4 );
 */
	pop_labels;
	kill_box;
	mode = edit;

/* Go through the list of global strings and load in the files.
Set the global strings to NULL when done in order to deallocate them. */
	jx = 1;
	while(  jx <= tcount  ) {
		Return_str = Global_Str('ML!!' + Str(jx));
		RM( 'LDFILES /CW=2' );
		Error_Level = 0;
		++jx;
	}
	RM('SetWindowNames');
	new_screen;
}

macro ARCDIR FROM ALL TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: ARCDIR

Description:	List an archive file.  Now supports both .ARC and .ZIP files.

Parameters:		MParm_Str = the archive file name.

Global Strings:
							'LZHDIR_COMMAND'  is the parameters for running the
								archive program to generate the list for .LZH files
							'ARCDIR_COMMAND'  is the parameters for running the
								archive program to generate the list for .ARC files.
							'ZIPDIR_COMMAND'  is the parameters for running the
								archive program to generate the list for .ZIP files.
								/D1=nn  The number of lines at the top of the list to
									delete.  This is to get rid of the startup message and
									other junk.
								/D2=nn  The number of lines at the bottom of the list to
									delete.  This gets rid of any junk at the bottom.
								/S1=nn  The number of lines at the top of the list to
									leave in but skip.  This is done after the deleting.
								/S2=nn  The number of lines at the bottom of the list to
									leave in but skip.  This is done after the deleting.
								/C=str  The command to user to run the program.  Must be
									the last parameter.
							 Example:
								 Set_Global_Str('ARCDIR_COMMAND',
											'/D1=6/D2=0/S1=2/S2=2/C=C:\UTIL\PKPAK -V' );

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  t_win, jx, d1, d2, s1, s2 ;
	str  arc_cmd[80],
			 tmp_file_name[80];
	refresh = false;
	t_win = cur_window;
	switch_window(window_count);
	create_window;
	working;
	if(  caps(get_extension(mparm_str)) == 'ZIP'  ) {
		arc_cmd = global_str('ZIPDIR_COMMAND');
		if(  arc_cmd == ''  ) {
			arc_cmd = '/D1=7/S1=2/S2=2/C=PKZIP.EXE -vb';
		}
	} else if( caps(get_extension(mparm_str)) == 'LZH' ) {
		arc_cmd = global_str('LZHDIR_COMMAND');
		if(  arc_cmd == ''  ) {
			arc_cmd = '/D1=7/S1=0/S2=0/C=LHA.EXE v';
		}
	} else {
		arc_cmd = global_str('ARCDIR_COMMAND');
		if(  arc_cmd == ''  ) {
			arc_cmd = '/D1=6/S1=2/S2=2/C=PKPAK -v';
		}
	}
	d1 = parse_int('/D1=', arc_cmd);
	d2 = parse_int('/D2=', arc_cmd);
	s1 = parse_int('/S1=', arc_cmd);
	s2 = parse_int('/S2=', arc_cmd);
	jx = xpos('/C=', arc_cmd, 1);

// Get correct .TMP file name according to user configuration
	RM("TMP_FILE_NAME /FN=DIR.TMP");
	tmp_file_name = return_str;

	arc_cmd = copy(arc_cmd, jx + 3, 80);
	make_message(arc_cmd + ' ' + mparm_str);
	return_str = arc_cmd + ' ' + mparm_str;

	RM('MEUTIL1^EXEC /MEM=12000/RED=' + tmp_file_name);
	load_file(tmp_file_name);
/*
	RM('MEUTIL1^EXEC /MEM=12000/RED=' + me_path + user_id + 'DIR.TMP' );
	load_file( me_path + user_id + 'DIR.TMP' );
*/
	jx = 0;
	while(  jx < d1  ) {
		++jx;
		del_line;
	}
	eof;
	jx = 0;
	while(  jx < d2  ) {
		++jx;
		del_line;
		up;
	}
	tof;
	jx = 0;
	while(  jx < s1  ) {
		++jx;
		eol;
		goto_col(100);
		text('|254');
		down;
	}
	goto_col(1);
	mark_pos;
	eof;
	jx = 0;
	while(  jx < s2  ) {
		++jx;
		goto_col(100);
		text('|254');
		up;
	}
	goto_mark;
	Set_Global_Str('AIPARM_1', '/TP=15/L=0/C=1/W=75/DC=1/WIN=' + str(cur_window) );
	RM('USERIN^DATA_IN /#=1/PRE=A/A=2/H=DIRSHELL^DOSELECT%ARC/T=' + dir_entry );
	refresh = false;
	delete_window;
	switch_window(t_win);
	make_message('');
	refresh = true;
}



macro DirFileView TRANS {
/*******************************************************************************
															MULTI-EDIT MACRO

Name: DIRFILEVIEW

Description: Allows user to view the file specified by RETURN_STR while in the
						 File Manager

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  o_win, omode, tx = wherex, ty = wherey ;
	str  tstr[128] ;

	if(  (File_Attr(Return_Str) & $18)  ) {
		RM('MEERROR^Beeps /C=1');
	} else {
		tstr = return_str;
		refresh = false;
		omode = mode;
		mode = edit;
		o_win = window_id;
		rm('LDFILES /LC=1/NC=1');
		read_only = TRUE;
		RM('USERIN^EDITWINDOW /X=5/Y=5/W=70/L=18/H=DIRSHELL^*/SE=1/T=' + tstr);
		refresh = false;
		mode = omode;
		read_only = false;
		delete_window;
		switch_win_id( o_win );
		gotoxy( tx, ty );
	}

}


macro tree TRANS {
/********************************MULTI-EDIT MACRO******************************

Name: TREE

Description:  Creates a graphic directory "tree".

Parameters:
							/X= X coordinate of box
							/Y= X coordinate of box
							/CP= Current path
							/CD= Current drive

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  level_count, level, tfsa, done,  tins, ttab, oline, orow, ocol,
					line_counter, wl, screen_full, old_time, old_undo, current_path_line,
					tr,jx,jy, x, y, w, l, wc, premature_done, key_press,
					old_window, event_count, dd_row, rebuild, tm,
					t_d_b_color = d_b_color,
					t_d_h_color = d_h_color,
					t_d_s_color = d_s_color,
					tree_window, old_refresh, building = false ;

	str  tstr[128], event_str[20],
					title[50], new_drive[2],
					cur_drive[2], tstr2,
					current_path[128], xstr[80] ;

	old_refresh = refresh;
	old_undo = undo_stat;
	undo_stat = FALSE;
	level = 0;
	tm = mode;
	mode = edit;
	tins = insert_mode;
	insert_mode = FALSE;
	ttab = tab_expand;
	tab_expand = false;
	done = FALSE;

	d_b_color = m_b_color;
	d_s_color = m_s_color;
	d_h_color = m_h_color;

	if(  global_str('@DIR_PARMS@') == ''  ) {
		RM('InitDirShell');
	}

	current_path = parse_str('/CP=', mparm_str);
	if(  current_path == ''  ) {
		current_path = get_path(dir_mask);
	}
	tstr = global_str('@TREE_PARMS@');
	tree_window = parse_int('/W=', tstr );
	cur_drive = parse_str('/CD=', tstr );
	new_drive = parse_str('/CD=', mparm_str );
	if(  new_drive == ''  ) {
		new_drive = get_path( dir_mask );
	}
	new_drive = copy(fexpand(new_drive), 1, 1 );
	rebuild = parse_int('/R=', tstr);
	current_path_line = parse_int('/CP=', tstr);
	dd_row = parse_int('/DDR=', tstr);
	x = parse_int('/X=', tstr );
	w = parse_int('/WD=', tstr );
	l = parse_int('/L=', tstr );
	y = parse_int('/Y=', tstr );
	if(  new_drive == ''  ) {
		new_drive = cur_drive;
	}
/*
	make_message( mparm_str );
 */
	if(  (new_drive != cur_drive) | (cur_drive == '')  ) {
 /*    new_drive := copy(fexpand('\'), 1, 1 ); */
		rebuild = TRUE;
	}

	premature_done = FALSE;
	oline = 1;
	orow = 1;
	ocol = 1;
	event_count = 0;
	event_str = '@EVTREE#';
	old_window = window_id;
	push_labels;
	flabel('CDrive', 2, -1 );
	tfsa = file_search_attr;
	file_search_attr = $10;
	refresh = false;

	if(  NOT(switch_win_id( tree_window ) )  ) {
		switch_window( window_count );
		create_window;
		tree_window = window_id;
		rebuild = TRUE;
		x = parse_int('/X=', mparm_str);
		y = parse_int('/Y=', mparm_str);
		w = 53;
		l = 20;
	}
	if(  x <= 0  ) {
		x = 2;
	}
	if(  y <= 0  ) {
		y = 2;
	}
	window_attr = $96;
	t_color = m_t_color;
	c_color = m_t_color;
	b_color = m_b_color;
	s_color = m_s_color;
	l_color = m_t_color;
	lb_color = m_t_color;
	eof_color = (m_t_color & $F0) + (m_t_color >> 4);

	call draw_tree_window;
	old_time = system_timer;
	if(  not(rebuild)  ) {
		set_mark(1);
		eof;
		if(  c_col == 1  ) {
			up;
		}
		line_counter = c_line;
		call reset_line_changed;
		call find_current_path;
		c_color = m_s_color;
		get_mark(1);
		call draw_display;
		update_status_line;
		refresh = true;
		redraw;
		call highlight;
		insert_mode = TRUE;
	}
loopx:
	if(  done  ) {
		goto exit;
	}
	if(  rebuild  ) {
		tstr = new_drive + ':\*.*';
		call do_rebuild;
		insert_mode = TRUE;
	}
	if(  check_key  ) {
		call master_key_loop;
	}
	goto loopx;

draw_display:
	refresh = false;
	RM('UpdateDrives /M=1/X1=' + str(x) + '/Y1=' + str(y) + '/X2=' +
				str( x + w ) + '/Y2=' + str( y + l ) + '/CD=' + new_drive );
	dd_row = return_int;
	size_window( x, dd_row + 1, x + w, y + l );
	wl = win_y2 - win_y1 - 2;
	ret;

write_building:
	title = '  Building Tree  ';
	write( title, wc - (svl(title) / 2), y, 0, working_color );
	ret;

do_rebuild:
	key_press = false;
	c_color = t_color;
	current_path_line = 0;
	premature_done = FALSE;
	call draw_display;
	rebuild = false;
	erase_window;
	put_line( new_drive + ':');
	set_mark(1);
	down;
	put_line( '' );
	refresh = true;
	redraw;
	insert_mode = false;
	screen_full = 0;
	line_counter = 1;
	call write_building;
	level = 0;
	building = true;
	call get_dir;
	building = false;
	title = 'ÄDirectory TreeÄÄ';
	write( title, wc - (svl(title) / 2), y, 0, b_color );
	update_status_line;
	call reset_line_changed;
	c_color = m_s_color;
	call find_current_path;
	get_mark(1);
	if(  NOT(key_press) & (current_path_line > 0)  ) {
		tof;
		down;
		down;
		down;
		down;
		goto_line( current_path_line );
		eol;
		left;
		goto_col( (((c_col - 1) / 13) * 13) + 1 );
	}
	refresh = true;
	redraw;
	cur_drive = new_drive;
	call highlight;
	ret;

get_dir:
	level_count = 0;
	if(  first_file( tstr ) == 0  ) {
loop:
		if(  screen_full  ) {
			if(  check_key  ) {
				tr = refresh;
				set_mark(2);
				get_mark(1);
				refresh = true;
				call master_key_loop;
				set_mark(1);
				refresh = tr;
				get_mark(2);
			}
		}
		if(  (last_file_attr & $10)  ) {
			if(  (last_file_name != '.') & (last_file_name != '..')  ) {
				if(  level > 0  ) {
					jx = level * 13;
					goto_col( jx  );
					up;
					if(  cur_char == 'À'  ) {
						text('Ã');
					} else if(  (cur_char == ' ') | (cur_char == '|255')  ) {
						if(  level_count == 0  ) {
							eol;
							text(copy('ÄÄÄÄÄÄÄÄÄÄÄÄ', 1, jx-  c_col) + '¿');
						} else {
							set_mark(3);
							while(  cur_char == ' '  ) {
								text('³');
								left;
								up;
							}
							if(  cur_char == 'À'  ) {
								text('Ã');
							}
							get_mark(3);
						}
					}
					down;
					goto_col( jx );
					text('À' + last_file_name);
				} else {
					goto_col(1);
					put_line( last_file_name );
				}
				if(  (line_counter >= wl)  ) {
					refresh = false;
					if(  screen_full == 0  ) {
						set_mark(2);
						refresh = true;
						screen_full = true;
						tof;
						call highlight;
						refresh = false;
						get_mark(2);
					}
					goto_line( line_counter + 2 );
				} else {
					DOWN;
				}
				++line_counter;
				create_global_str( '@FILEREC@' + str(level), file_search_rec );
				create_global_str( '@FILETSTR@' + str(level), tstr );
				tstr = get_path(tstr) + last_file_name + '\*.*';
				++level;
				call get_dir;
				--level;
				file_search_rec = global_str( '@FILEREC@' + str(level));
				tstr = global_str( '@FILETSTR@' + str(level));
				set_global_str( '@FILEREC@' + str(level), '');
				set_global_str( '@FILETSTR@' + str(level), '' );
				++level_count;
			}
		}
		if(  NOT(done) & (next_file == 0) & NOT(rebuild)  ) {
			goto loop;
		}
		premature_done = DONE;
	}
	ret;

find_current_path:
	set_mark(4);
	current_path_line = 0;
	if(  copy(current_path, 1, 1) == cur_drive  ) {
		tof;
		tstr = copy(current_path, 4, 254);
		jx = xpos('\', tstr, 1 );
		if(  jx == 0  ) {
			jx = svl(tstr) + 1;
		}
		if(  search_fwd('%' + copy(tstr,1, jx - 1), 0)  ) {
		fcp_loop:
			call create_return_str;
			if(  return_str == current_path  ) {
				line_changed = TRUE;
				current_path_line = c_line;
			} else {
				call skip_right;
				if(  (c_col > 1) & (NOT(at_eof))  ) {
					goto fcp_loop;
				}
			}
		}
	}
fcp_exit:
	get_mark(4);
	ret;


master_key_loop:
	key_press = TRUE;
	call lowlight;
	if(  key1 == 27  ) {
	 esc:
		done = TRUE;
		return_int = 0;
	} else if(  key1 == 13  ) {
	 enter:
		return_int = TRUE;
		done = TRUE;
		call create_return_str;
	} else if(  key1 == 0  ) {
		if(  (key2 == 60)  ) {
			call get_new_drive;
		} else if(  (key2 == 80) & ( c_line < (line_counter) )  ) {
			call skip_down;
		} else if(  (key2 == 72)  ) {
			call skip_up;
		} else if(  (key2 == 77)  ) {
			call skip_right;
		} else if(  (key2 == 75)  ) {
			call skip_left;
		} else if(  (key2 == 71)  ) {
			tof;
		} else if(  (key2 == 79)  ) {
			eof;
			down;
			goto_col(1);
			call skip_right;
		} else if(  (key2 ==  73)  ) {
			page_up;
			call skip_up;
		} else if(  (key2 == 81)  ) {
			page_down;
			call skip_down;
		} else if(  (key2 == 238)  ) {
			jx = 0;
			call Do_Resize;
		} else if(  (key2 == 250)  ) {
			call mouse_event;
		} else {
			goto check_for_key_macro;
		}
	} else {
check_for_key_macro:
		if(  inq_key( key1, key2, DOS_SHELL, xstr ) > 0  ) {
			pass_key(key1, key2);
		}
	}
	call highlight;
	ret;


lowlight:
	if(  line_changed  ) {
		draw_attr( wherex, wherey, c_color, 12 );
	} else {
		draw_attr( wherex, wherey, t_color, 12 );
	}
	ret;


create_return_str:
	return_str = '';
	set_mark(2);
crs_loop:
	refresh = false;
	ocol = c_col;
	return_str = get_word('Ä¿ |255|9') + '\' + return_str;
	if(  ocol > 1  ) {
		goto_col(ocol - 13);
		call skip_up;
		goto crs_loop;
	}
	refresh = false;
	if(  copy( return_str, 2, 1) != ':'  ) {
		return_str = cur_drive + ':\' + return_str;
	}
	get_mark(2);
	ret;

skip_down:
	down;
skip_down2:
	if(  (copy(get_line,c_col,12) == '            ') | at_eol  ) {
		if(  c_col > 1  ) {
			left;
			if(  cur_char == '³'  ) {
				while(  NOT( at_eof ) & (cur_char == '³')  ) {
					DOWN;
				}
				right;
				if(  at_eof  ) {
					goto skip_up;
				}
				goto skip_down2;
			}
			goto_col( c_col - 12);
			if(  at_eof  ) {
				goto skip_up;
			}
			goto skip_down2;
		} else {
			if(  (at_eof)  ) {
				goto skip_up;
			} else {
				goto skip_down;
			}
		}
	}
	ret;

skip_up:
	up;
skip_up2:
	if(  (copy(get_line,c_col,12) == '            ') | at_eol  ) {
/* If we don't see the next level, keep looking */
		if(  c_col > 1  ) {
			left;

			if(  xpos(cur_char,' ³',1)  ) {
				while(  ( c_line > 1 ) & (xpos(cur_char,'³ ',1) != 0)  ) {
					UP;
				}
				RIGHT;
				goto skip_up2;
			}

/*
			IF cur_char = '³' THEN
				WHILE ( c_line > 1 ) and (cur_char = '³') DO
					UP;
				END;
				RIGHT;
				goto skip_up2;
			END;
 */
			goto_col( c_col - 12);
			goto skip_up2;
		} else {
			if(  c_line > 1  ) {
				goto skip_up;
			}
		}
	}
	ret;

skip_right:
	down;
	refresh = false;
	goto_col( c_col + 13 );
	if(  at_eol  ) {
skip_right2:
		eol;
		left;
		goto_col( (((c_col - 1) / 13) * 13) + 1 );
	}
	if(  at_eof  ) {
		call skip_up;
	}
	refresh = TRUE;
	ret;

skip_left:
	UP;
	eol;
	left;
	goto_col( (((c_col - 1) / 13) * 13) + 1 );
	ret;

mouse_event:
	jx = c_line;
	ocol = c_col;
	RM('MOUSE^MouseInWindow');
	if(  (return_int == 1)  ) {
		goto_col( (((c_col - 1) / 13) * 13) + 1 );
		if(  (jx == c_line) & (ocol == c_col)  ) {
			done = TRUE;
			call create_return_str;
		} else {
			call skip_up2;
		}
	} else if(  (Mou_Last_Y == Fkey_Row)  ) {
		RM( 'MOUSE^MouseFkey' );
	} else if(  (Mou_Last_y == y)  ) {
		if(  (Mou_Last_X > X) & (Mou_Last_X < (x + w))  ) {
			jx = 0;
			call Do_Resize;
		} else if(  (mou_last_x == x)  ) {
			jx = 1;
			call do_resize;
		} else if(  (mou_last_x == (x + w))  ) {
			jx = 2;
			call do_resize;
		}
	} else if(  (Mou_Last_Y == Win_Y2) & (Mou_Last_X == (x + w))  ) {
		jx = 4;
		call do_resize;
	} else if(  (Mou_Last_Y == Win_Y2) & (Mou_Last_X == x)  ) {
		jx = 3;
		call do_resize;
	} else if(  (Mou_Last_X == Win_X2)  ) {
		RM('MOUSE^HandleScrollBar /EOF=1/L=' + str(line_counter));
		if(  (return_int == 1)  ) {
			call skip_right2;
		} else if(  (return_int == 2)  ) {
			call skip_right2;
		}
	} else if(  (Mou_Last_Y > y) & (Mou_Last_Y <= dd_row)  ) {
		jx = (Mou_Last_Y - y - 1) * ( w / 3);
		JX =  (((Mou_Last_X - x) - 1) / 3) + 1 + jx;
		tstr = parse_str('/DS=',global_str('@DIR_PARMS@'));
		if(  jx <= svl( tstr )  ) {
			new_drive = str_char( tstr, jx );
			rebuild = TRUE;
		}
	} else if(  (Mou_Last_X > X) & (Mou_Last_Y <= (X + W))  ) {
		RM('CheckEvents /M=1/G=' + event_str + '/#=' + str(event_count));
		if(  RETURN_INT != 0  ) {
			RETURN_INT = Parse_Int('/R=', Return_Str );
			if(  return_int == 1  ) {
				done = TRUE;
				call create_return_str;
			} else if(  return_int == 0  ) {
				done = TRUE;
			}
		}
	} else if(  (Mou_Last_X < X) | (Mou_Last_X > (X + W + 3))
			| (Mou_Last_Y < Y) | (Mou_Last_Y > (WIN_Y2 + 1))  ) {
		Push_Key(0,250);
		RETURN_INT = 0;
		done = TRUE;
	}
	ret;


draw_tree_window:
	if(  w < 26  ) {
		w = 26;
	}
	if(  l < 6  ) {
		l = 6;
	}
	if(  (y + l) > (max_window_row)  ) {
		if(  y >= max_window_row  ) {
			y = max_window_row - 10;
		}
		l =  max_window_row - y - 1;
	}
	if(  (x + w) > (screen_width)  ) {
		if(  x >= (screen_width - 20)  ) {
			x = screen_width - 42;
		}
		w =  screen_width - x - 2;
	}
	put_box( x, y, x + w + 2, y + l + 1, 0, m_b_color, 'Directory Tree', TRUE);
	wc = (x + (w / 2)) + 1;
	Set_Global_Str(event_str + '1',
			'/T=Cancel/KC=<ESC>/W=11/K1=27/K2=1/R=0/Y=' +
									str(y + l) +
									'/X=' + str( wc - 13));
	Set_Global_Str(event_str + '2',
			'/T=Select/KC=<ENTER>/W=13/K1=27/K2=1/R=1/Y=' +
									str(y + l) +
									'/X=' + str( wc - 1));
	event_count = 2;
	RM('CheckEvents /M=2/G=' + event_str + '/#=' + str(event_count));
	ret;

do_resize:
	refresh = false;
	if(  jx == 0  ) {
		jy = 2;
	} else {
		jy = 0;
	}
	RM('WINDOW^MOVE_WIN /X1=' + str(x)+'/Y1=' + Str(y) + '/X2=' +
		Str(x + w) + '/Y2=' + Str(y + l) + '/MS=' + str(0) +
		'/MX1=1/MX2=' +	Str(Screen_Width) +
		'/MY1=2/MY2=' + str(screen_length - 1) +
		'/MM=' + str(jx) + '/M=' + str(jy) + '/K=' + str(return_int));
	x = parse_int('/X1=', return_str);
	y =  parse_int('/Y1=', return_str);
	w = parse_int('/X2=', return_str) - x;
	l = parse_int('/Y2=', return_str) - y;
	if(  (jy == 2) & (virtual_display != 0)  ) {
		override_screen_seg;
		call draw_tree_window;
		reset_screen_seg;
	} else {
		kill_box;
		set_virtual_display;
		call draw_tree_window;
	}
	call draw_display;
	update_status_line;
	refresh = true;
	redraw;
	call highlight;
	if(  building  ) {
		call write_building;
	}
	update_virtual_display;
	reset_virtual_display;
	ret;

highlight:
	if(  (refresh == FALSE) | (DONE)  ) {
		ret;
	}
	ocol = c_col;
	goto_col( c_col + 12 );
	goto_col(ocol);
	draw_attr( wherex, wherey, m_h_color, 12 );
	ret;

get_new_drive:
		tstr = parse_str('/DS=',global_str('@DIR_PARMS@'));
		jx = svl(tstr);
		while(  jx > 0  ) {
			tstr = str_ins( ':()', tstr, jx + 1 );
			--jx;
		}
		RM('USERIN^XMENU /B=1/L=Select Drive/M=' + tstr);
		if(  return_int > 0  ) {
			tstr = parse_str('/DS=',global_str('@DIR_PARMS@'));
			new_drive = str_char( tstr, return_int );
			rebuild = TRUE;
		}
		ret;

reset_line_changed:
		mark_pos;
		tof;
		while(  NOT(at_eof)  ) {
			line_changed = FALSE;
			down;
		}
		goto_mark;
		ret;

exit:
	d_b_color = t_d_b_color;
	d_s_color = t_d_s_color;
	d_h_color = t_d_h_color;
	tstr = '/W=' + str(tree_window) + '/CD=' + cur_drive + '/DDR=' + str(dd_row) +
		'/X=' + str(x) + '/Y=' + str(y) + '/L=' + str(l) + '/WD=' + str(w);

	if(  premature_done  ) {
		erase_window;
		tstr = tstr + '/R=1';
	}
	set_global_str('@TREE_PARMS@', tstr);
	pop_labels;
	RM('CheckEvents /M=3/G=' + event_str + '/#=' + str(event_count));
	refresh = false;
	window_attr = $81;
	kill_box;
	switch_win_id( old_window );
	refresh = old_refresh;
	file_search_attr = tfsa;
	insert_mode = tins;
	tab_expand = ttab;
	undo_stat = old_undo;
	mode = tm;
	if(  return_int  ) {
		if(  parse_int('/SD=', mparm_str)  ) {
			working;
			rm('PROCESSDIR');
		}
	}
}

macro FILEATTR TRANS {
/********************************MULTI-EDIT MACRO******************************

Name: FILEATTR

Description:  Allows user to view/change file attributes.

Parameters:
							Return_Str = The default file name to change file attributes of.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str F_Name[80];
	int T_Attr;

	RM('USERIN^QUERYBOX /C=15/W=40/T=FILE TO VIEW////CHANGE ATTRIBUTES/F2=Dir   /L=3/H=DIRSHELL^VIEWATTR');
	if(  Not(Return_Int)  ) {
		Goto EXIT;
	}

	F_Name = Caps(Return_Str);

	T_Attr = File_Attr(F_Name);
	if(  (Error_Level)  ) {
		RM('MEERROR');
		Return_Int = 0;
		Goto EXIT;
	}
	if ( T_Attr & $18 ) {
		RM('MEERROR^MESSAGEBOX /B=1/M=' + F_Name + 'is a ' +
				Copy('Volume LabelDirectory',(((T_Attr & $18) >> 4) * 12) + 1,12) +
				'!  Changing attributes is not allowed.');
		Return_Int = 0;
		Goto EXIT;
	}

	Set_Global_Str('IPARM_1','/C=1/L=2/TP=10/T=' + f_name);
	Set_Global_Int('IINT_2',(T_Attr & $20) >> 5);
	Set_Global_Str('IPARM_2','/C=10/L=4/H=/TP=13/QK=1/T=Archive   ');
	Set_Global_Int('IINT_3',T_Attr & $01);
	Set_Global_Str('IPARM_3','/C=10/H=/TP=13/QK=1/T=Read Only ');
	Set_Global_Int('IINT_4',(T_Attr & $02) >> 1);
	Set_Global_Str('IPARM_4','/C=10/H=/TP=13/QK=1/T=Hidden    ');
	Set_Global_Int('IINT_5',(T_Attr & $04) >> 2);
	Set_Global_Str('IPARM_5','/C=10/H=/TP=13/QK=1/T=System    ');

	RM('USERIN^DATA_IN /A=0/#=5/H=DIRSHELL^VIEWATTR/S=1/X=4/Y=5/T=FILE ATTRIBUTES');


	if ( !Return_Int )
		Goto EXIT;

	Set_File_Attr(F_Name,(Global_Int('IINT_2')<<5)+Global_Int('IINT_3')+
		(Global_Int('IINT_4')<<1)+(Global_Int('IINT_5')<<2));
	if ( Error_Level ) {
		RM('MEERROR');
		Return_Int = 0;
		Goto EXIT;
	}

EXIT:
	for ( t_attr = 2; t_attr < 6; t_attr++ )
		set_global_int('IINT_'+str(t_attr), 0);
 }

/********************************MULTI-EDIT MACRO******************************

Name:  CheckPSInstalled

Description:  Checks to see if PRINT.COM has been installed.

Parameters:		NONE

Returns:			Return_Int = 0 - PRINT.COM not installed
													 1 - PRINT.COM is installed

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro CheckPSInstalled {
	if ( 0x0200>(((DOS_Version&0xff)<<8)|((DOS_Version&0xff00)>>8)) ) {
		RM('MEERROR^MESSAGEBOX /B=2/T=ERROR/M=DOS version 2.00 and up is required.');
		Return_Int = 0;
	}
	R_AX = $0100;	Intr(0x2F);
	if ( ((R_AX & $FF) != $FF) ) {
			RM('MEERROR^MESSAGEBOX /B=2/T=ERROR/M=DOS Print Spooler has not been loaded.  Please exit Multi-Edit and load the print spooler before printing a file.');
			Return_Int = 0;
	}
	else
		Return_Int = 1;
}
