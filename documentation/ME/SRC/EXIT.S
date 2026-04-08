macro_file EXIT;

#INCLUDE MESYS.SH

/*******************************************************************************
													MULTI-EDIT MACRO FILE EXIT

EXIT		- Standard exit routine

StrCrc16 - Converts a string into a 16bit CRC value.
CrcFileStr - Creates a CRC16 based filename
make_restr_name - Creates the name for the restore file

RESTORE - Restores editor according to file created by STATUS
STATUS	- Creates restore file.

							 (C) Copyright 1991-93 by American Cybernetics, Inc.
*******************************************************************************/

macro EXIT {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	EXIT

Description:  If any files are changed and not saved this macro displays them
							and asks for user verification.  The user is given the choice of
							quitting without saving, saving and then quitting, or cancelling
							the operation.

Parameters:
							/BC=  Only relevant when called via the menu system.  Tells EXIT
										how many boxes to kill.
							/NP=  If 1, and there are modified files in memory, the usual
										"Do you want to save these files?" prompt will not appear,
										but the files will be unconditionally saved.

              /NE=1 Go through all of the exit procedures, but do not actually
                    quit.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int Old_Refresh,Temp_Integer,Not_Saved,Temp_Window,X1,X2,Y1,Y2,Str_X,
					 t_bc, jx, jy, jz, choice = global_int("!EXIT_PROMPT_CHOICE") ;
	str Temp_String;

	Old_Refresh = Refresh;
	Refresh = False;
	Temp_Window = CUR_WINDOW;
	working;

	temp_string = global_str('@ME_EXIT_MACRO@');
	while(  temp_string != ''  ) {
		jx = xpos( ';', temp_string, 1 );
		if(  jx == 0  ) {
			jx = svl(temp_string) + 1;
		}
		Return_Int = 1;
		if(  jx > 1  ) {
			RM( copy( temp_string, 1, jx -1)  );
		}
		temp_string = str_del( temp_string, 1, jx );
		if(  return_int == 0  ) {
			goto end_of_mac;
		}
	}

	t_bc = Parse_Int('/BC=', mparm_str);
	while(  box_count > t_bc  ) {
		kill_box;
	}

/* Check to see how many files are not saved */
	Temp_Integer = 1;
	Not_Saved = 0;
	while(  (Temp_Integer <= Window_Count)  ) {
		Switch_Window(Temp_Integer);
		if(  (File_Changed)  ) {
			if(  (window_attr & $80) == 0  ) {
					Not_Saved = Not_Saved + 1;
			} else {
				if(  caps( file_name ) != '?NO-FILE?') {
					save_file;
				}
			}
		}
		++Temp_Integer;
	}

	if(  (Not_Saved > 13)  ) {
		Not_Saved = 13;
	}

/* If there are any unsaved files, make a warning box */
	if(  (Not_Saved)  ) {
		if(  (parse_int('/NP=',mparm_str))  ) {
			goto skip_prompt;
		}

		switch_window( window_count );
		create_window;
		jy = cur_window;
		jz = 1;

		JX = 1;
		while(  jx < window_count  ) {
			switch_window( jx );
			if(  (window_attr & $80) == 0  ) {
				if(  (File_Changed)  ) {
					put_line_to_win( window_name + '    ' + File_Name, jz, jy, FALSE );
					++jz;
				}
			}
			++jx;
		}
		switch_window( jy );
		if(global_int("@DIALOG_STYLE") == 1)
		{
			if(choice > 3)
				choice = 3;
			if (choice < 0) {
				choice = 1;
			}
			set_global_str('IPARM_1','/TP=10/L=2/T=Save files?/C=1' );
			set_global_str('IPARM_2','/TP=11/L=2/T= Yes /QK=2/R=3/C=14' );
			set_global_str('IPARM_3','/TP=11/L=2/T= No /QK=2/R=1/C=21' );
			set_global_str('IPARM_4','/TP=11/L=2/T=Cancel/R=0/KC=<ESC>/C=27' );
			set_global_str('IPARM_5','/TP=11/L=2/T=Help/R=2/KC=<F1>/C=48' );
			set_global_str('IPARM_6','/TP=15/W=56/L=5/QK=1/T=Modified files:/C=1/WIN=' + str(cur_window) );
			RM('USERIN^DATA_IN /H=QUIT/#=6/T=Files have been modified!/S=' +
					str(choice + 1));
		}
		else
		{
			if(choice > 3)
				choice = 3;
			if (choice < 1) {
				choice = 3;
			}
			set_global_str('IPARM_1','/TP=11/L=2/T= Yes /QK=2/R=1/W=6/C=12' ); set_global_str('IPARM_2','/TP=11/L=2/T= No
			/QK=2/R=0/K1=27/K2=1/W=5/C=19' ); set_global_str('IPARM_3','/TP=11/L=2/T= Save files and quit /R=3/QK=2/W=10/C=25' );
			set_global_str('IPARM_4','/TP=15/W=56/L=4/QK=1/T=Modified files:/C=1/WIN=' + str(cur_window) ); RM('USERIN^DATA_IN
			/H=QUIT/#=4/T=FILES NOT SAVED!  ARE YOU SURE YOU WANT TO EXIT?/S=' + str(choice)); } refresh = FALSE; delete_window;
		Temp_Integer = return_int; if(  (Temp_Integer == 3)  ) { skip_prompt:  working; Make_Message('Saving files...');
			RM('AUTOSAVE');

			if(  (Return_Int != 0)  ) {
				Make_Message('Exit aborted.');
				Goto END_OF_MAC;
			}
			Goto Go_Quit;
		}
		if(  (Temp_Integer == 0)  ) {
			Goto End_OF_Mac;
		}
		if(  (Temp_Integer == 1)  ) {
			Goto Go_Quit;
		}
	} else {
		Goto Go_Quit;
	}
	Goto End_OF_Mac;
GO_QUIT:

	Switch_Window(Temp_Window);

	Return_Int = 1;
	rm('setup^check_setup /X=10/Y=3');
	if(  return_int < 1  ) {
		goto End_Of_Mac;
	}
	working;
	if(  Global_Int('RESTORE')  ) {
		Set_Global_Str('@TREE_PARMS@', '');
		Set_Global_Str('COM_LINE_PARAMS', '');


			/* Clear out unneeded globals */

		JX = 12;
		temp_string = '0ISTR_';
		CALL CLEAR_GLOBAL_LIST;

		JX = 12;
		temp_string = 'X0ISTR_';
		CALL CLEAR_GLOBAL_LIST;

		jx = global_int('@KEYMACRO_COUNT@');
		temp_string = '@KM!#';
		CALL CLEAR_GLOBAL_LIST;

		set_global_int('@KEYMACRO_COUNT@', 0 );
		set_global_str('@KMTEMP!#','');


		RM('STATUS');

	}
Quit2:

	refresh = false;

  if( !parse_int("/NE=", mparm_str))
  {
    status_row = 0;
    Rest_Dos_Screen;
    QUIT(0);
  }

  return_int = 1;
	goto end_of_mac;

CLEAR_GLOBAL_LIST:
	JY = 0;
	while(  JY < JX  ) {
		++JY;
		SET_GLOBAL_STR( temp_string + STR(JY), '' );
	}
	RET;


END_OF_MAC:
	Switch_Window(Temp_Window);
	Refresh = Old_Refresh;

}

/*---------------02-07-93 08:07pm-------------------
 StrCrc16

		Calculates the CRC16 value of the passed string.
		Returns the integer result.
--------------------------------------------------*/
int StrCrc16( str szBuf )
{
  int crc = 0,
			temp;
  int i, j, iBufLen, ulTableEntry;

  // Calculate the crc value.
  iBufLen = svl( szBuf );
  for ( i = 1; i <= iBufLen; i++)
  {
		Temp = ((crc >> 8) & $FF) ^ ascii(  str_char( szbuf, i ));
		crc = ((crc << 8) & 0xFFFF) ^ get_crc_entry(Temp);
  }

  return( crc );
}


/*---------------02-07-93 10:19pm-------------------
 CrcFileStr
		Creates an 8 character unique name from the
		passed directory/file name.
--------------------------------------------------*/
str CrcFileStr( str dn )
{
	str fn[8] = "", crc[4];

	int jx;

	jx = xpos( "\\", dn, 4 );
	if( jx > 0 )
	{
		crc = Hex_Str( StrCrc16( copy( dn, jx + 1, 255 ) ) & 0xFFFF );

		crc = copy("0000", 1, 4  - svl(crc)) + crc;
		fn = crc;
		dn = copy( dn, 1, jx - 1 );
	}
	else
	{
		fn = "0000";
	}
	crc = Hex_Str( StrCrc16( dn ) & 0xFFFF);
	crc = copy("0000", 1, 4  - svl(crc)) + crc;
	return( crc + fn );
}


/*---------------02-07-93 10:24pm-------------------
 make_restr_name

			Creates the proper name and path for the
			restore file.  Does not include an extension!

			restore_mode defines how the name is to be
			created.
				1 = The old restore_path + user_id + STATUS
				2 = ME_PATH + user_id + STATUS
				3 = The new CRC encode name
--------------------------------------------------*/
str make_restr_name( int restore_mode )
{
  str n[8],
      restore_path[128];

  return_str = Global_Str("@RESTORE_PATH");
  rm("XlateCmdLine");
  restore_path = return_str;

  if( (svl(restore_path) != 0) &&
    (copy(restore_path,length(restore_path),1) != '\')
    )
    restore_path += '\';

	if( restore_mode == 3 )
	{
		n = CrcFileStr( fexpand("") );
    if( restore_path != "" )
		{
      return_str = restore_path + n;
		}
		else
		{
			return_str = n;
			rm("MakeUserPath");
		}
	}
	else
	{
    if(  restore_mode == 2  ) {
      if( restore_path != '' )
      {
        return_str = restore_path + user_id + "STATUS";
      }
      else
      {
        return_str = "STATUS";
        rm("MakeUserPath");
      }
		} else {
      return_str = user_id + 'STATUS';
		}
	}
	return( return_str );
}


macro RESTORE TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	RESTORE

Parameters:  /M=    Mode
								0 = Normal full restore
								1 = Partial restore.  Restore globals, and general status,
										but NOT windows and files.

Description: This macro uses the file STATUS.ME and restores the editor exactly
						 to the conditions contained therein.


--------------------------- MODIFICATIONS --------------------------------------

040593[scm]:
 Returns:      Global system variable Return_Int is set to the active window
							 (the window that is active upon entry to the this macro)
							 This change aids the Session Manager.

-------------------------------------------------------------------------------

							 (C) Copyright 1991-93 by American Cybernetics, Inc.
*******************************************************************************/
  int Temp_Integer,Status_Window,Active_Window = 0,Temp_Window,X1,Y1,X2,Y2,
          count, t_sc, new_screen_setup = 0, trestore = global_int("RESTORE"),
          restore_mode = parse_int("/M=", mparm_str), bid_count = 0,
          new_screen_layout = 0;
	str  TStr[500], numstr[20] ;

	Refresh = False;
	Messages = False;
	Error_Level = 0;
	working;
/* Turn truncate spaces off while loading STATUS.ME lest certain global strings
with trailing spaces get screwed up. */
	Temp_Integer = Truncate_Spaces;
	Truncate_Spaces = False;
	if( restore_mode )
	{
		Active_Window = window_id;
		create_window;
	}
	Load_File( make_restr_name( global_int("RESTORE")) + '.ME' );

/*
	if(  Global_Int('@RESTORE_USE_ME_PATH')  ) {
		Load_File(me_path + user_id + 'STATUS.ME');
	} else {
		Load_File( Global_Str('@RESTORE_PATH') + user_id + 'STATUS.ME');
	}
*/

	Truncate_Spaces = Temp_Integer;
	Status_Window = Cur_Window;
	if(  (ERROR_LEVEL != 0)  ) {
		Temp_Integer = error_level;
		refresh = TRUE;
		Error_Level = 0;
		rm('WINDOW^DELWIN');
		if(  (temp_integer == 3002)  ) {
			Error_Level = 0;
		} else {
			RM('MEERROR^MessageBox /B=2/M=ERROR OCCURRED DURING RESTORE MACRO!  Press any key for error message.');
			Error_level = temp_integer;
		}
		Goto END_OF_MAC;
	}
	if(  remove_Space(copy(get_line,1, 23)) !=
			 remove_space('@MULTI-EDIT VERSION ' + Copy(version,1,3))  ) {
		refresh = TRUE;
		rm('WINDOW^DELWIN');
		Make_Message( ' STATUS FILE CREATED WITH DIFFERENT VERSION.  RESTORE ABORTED.');
		Goto END_OF_MAC;
	}

	if(!restore_mode)
	{
		Make_Message('Restoring previous status...');
		working;
	}
	window_attr = $81;

	reg_exp_stat = true;
	ignore_case = false;

	Set_Virtual_Display;
	down;
	tstr = get_line;
	down;
	if(  Parse_Int('/MISC=',TStr)  ) {
		Insert_Mode = Parse_Int('/I=',TStr);
		var_parse_int('/CC=', tstr, x1 );
    new_screen_layout = (global_int("CUR_SCRN") != x1);
		set_global_int('CUR_SCRN', x1 );
		var_parse_int('/SC=', tstr, t_sc );
		var_parse_int('/V=', tstr, x1 );
		var_parse_int('/EV=', tstr, y1 );
		if( ( x1 != video_mode ) || ( y1 != ext_video_mode ) ) {
			Reset_Virtual_Display;
			ext_video_mode = y1;
			set_video_mode( x1 );
			new_screen_setup = TRUE;
		}
	}

	if( new_screen_setup )
	{
//    RM('SETSCRN');
    SETSCRN(0,0,0,"",0,0,0);
		// mouse = mouse;
		working;
		if(!restore_mode)
			Make_Message('Restoring previous status...');
		Set_Virtual_Display;
  } else if (new_screen_layout) {
//    RM('SETSCRN');
    SETSCRN(0,0,0,"",0,0,0);
  }

	// Now, if partial restore only, then skip the window stuff
	if( restore_mode == 1 )
	{
		search_fwd("%/G_???=", 0);
	}
	else
	{
		count = 0;
		while(  NOT( At_EOF )  ) {
			TStr = Get_Line;
			if(  Copy( tstr, 1,3) == '/W=') {
				WORKING;
				Temp_Integer = (Parse_Int('/A=',TStr) == 1);
				Switch_Window( Window_Count );
				Create_Window;
				if(  temp_integer  ) {
					active_window = window_id;
				}
				Temp_Window = Cur_Window;
				temp_integer = XPOS('/ZOOM=', tstr, 1 );
				if(  temp_integer != 0  ) {
					set_global_str('!WINZOOM#' + str(window_id),
													copy(tstr,temp_integer + 6, 80 ) );
					tstr = copy(tstr, 1, temp_integer - 1);
					if(  global_str('!WINZOOM#' + str(window_id)) != ''  ) {
						zoom_char = parse_str( '/ZC=', Global_Str('!WINZOOM#' + str(window_id)) );
					}
				}
				File_Name = Parse_Str('/FN=',TStr);

	/* Restore window coordinates while checking for out of bounds */
				Var_Parse_Int('/X1=',TStr, x1);
				if(  (X1 < Min_Window_Col)  ) {
					X1 = Min_Window_Col;
				}
				Var_Parse_Int('/Y1=',TStr, y1);
				if(  (Y1 < Min_Window_Row)  ) {
					Y1 = Min_Window_Row;
				}
				Var_Parse_Int('/X2=',TStr, x2);
				if(  (X2 > Max_Window_Col)  ) {
					X2 = Max_Window_Col;
				}
				Var_Parse_Int('/Y2=',TStr, y2);
				if(  (Y2 > Max_Window_Row)  ) {
					Y2 = Max_Window_Row;
				}
				if(  x2 != 0  ) {
					Size_Window(X1,Y1,X2,Y2);
				}
				if(  (Parse_Int('/LS=',TStr))  ) {
	/* If file is linked to another see if it is the first occurance or not */
          if(  (Global_Int('!Buffer_Id' + Parse_Str('/BI=',Tstr)))  ) {
	/* If not, link it to the window with the first occurance but don't load a file */
            Link_Window(Global_Int('!Buffer_Id' + Parse_Str('/BI=',Tstr)));

            // Check for a hex window
            if( (global_int('!Buffer_Hex' + Parse_Str('/BI=', tstr)))
                || (Parse_Int('/WA=',TStr) & 0x20))
            {
              temp_integer = window_id;
              switch_window( Global_Int('!Buffer_Id' + Parse_Str('/BI=',Tstr)));
              event_macro = "Hex^HexEventProc /W=" + str(temp_integer  );
              temp_integer = window_id;
              switch_window(temp_window);
              event_macro = "Hex^HexEventProc /W=" + str(temp_integer  );
            }
					} else {
	/* If so, set a global integer to indicate a first occurance and to store the
	window number of that one and then load the file */
            ++bid_count;
            set_global_int('!BID_' + str(bid_count), Parse_Int('/BI=',Tstr));
            Set_Global_Int('!Buffer_Id' + Parse_Str('/BI=',Tstr),Cur_Window);
            if( parse_int('/WA=', tstr) & 0x20)
              set_global_int('!Buffer_Hex' + Parse_Str('/BI=', tstr), 1);
						Goto LOAD_IT;
					}
				} else {
	LOAD_IT:
          if(parse_str('/LT=', tstr) != "" )
          {
            line_terminator = reconvert_string(parse_str('/LT=', tstr ));
          }
          fixed_line_length = parse_int("/FLL=", tstr );
					if(  (File_Name != '?No-File?')  ) {
            XLoad_File(File_Name);
					}
				/*  reset_virtual_display;  */
          RM( 'EXTSETUP /FTO=255');
				/*  set_virtual_display;   */
				}
				File_Changed = False;
				Goto_Col( Parse_int('/IL=',TStr));
				Set_Indent_Level;
				Var_Parse_Int('/R=',TStr, x1);
				while(  C_Row < X1  ) {
					X2 = C_ROW;
					Down;
					if(  X2 == C_Row  ) {
						break;
					}
				}
				// check for split error windows (kind of like split hair windows)
				if( parse_int('/ERWN=',tstr)) {
					set_global_int('~MEERR_ID', window_id);
				}
				else if (parse_int('/ERSPLT=', tstr) ) {
					set_global_int('~MEERR_SPLIT_ID',window_id );
				}
				Wrap_Stat = Parse_Int('/WS=',TStr);
				Right_Margin = Parse_Int('/RM=', tstr );
				Indent_Style = Parse_Int('/IS=',TStr);
				Var_Parse_Int('/D=', tstr, x1);
				if(  x1 > 0  ) {
					Doc_Mode = x1;
				}
				Block_Stat = Parse_Int('/BS=',TStr);
				if(  block_stat != 0 ) {
					block_line1 = Parse_Int('/BL1=',TStr);
					block_line2 = Parse_Int('/BL2=',TStr);
					block_col1 = Parse_Int('/BC1=',TStr);
					block_col2 = Parse_Int('/BC2=',TStr);
				}
				Goto_Col( Parse_Int('/C=',TStr));
				Goto_Line( Parse_Int('/L=',TStr));
				Window_Attr = Parse_Int('/WA=',TStr);
				w_Bottom_line = Parse_int('/BTML=', tstr);
				window_name = Parse_Str('/WNM=', tstr);
				screen_num = parse_int('/SN=', tstr );
				if(  parse_int('/WCS=', tstr)  ) {
					window_color_stat = TRUE;
					t_color = parse_int('/CTC=',tstr);
					c_color = parse_int('/CCC=',tstr);
					b_color = parse_int('/CBC=',tstr);
					h_color = parse_int('/CHC=',tstr);
					s_color = parse_int('/CSC=',tstr);
					l_color = parse_int('/CLC=',tstr);
					lb_color = parse_int('/CLBC=',tstr);
					eof_color = parse_int('/CEC=',tstr);
				}
      	if( window_id == Active_Window )
      	{
        	refresh = true;
        	redraw;
        	refresh = false;
        	update_virtual_display;
      	}
			} else if( copy(  tstr, 1, 7 ) == '/MARKS=' ) {
				int jz, jk, line;
				Switch_Window( temp_window );
					// here is where we restore the position markers
				mark_stack_count = parse_int( '/MARKS=', tstr );
				for( jk = 1; jk <= 3; ++jk)
					for( jz = 1; jz <= 10; ++jz ) {
						numstr = str(jz + (10 * (jk - 1))) + '=';
						line = parse_int('/L' + numstr, tstr );
						if( line > 0 ) {
							set_mark_record( jz, jk, line, parse_int('/P' + numstr, tstr ),
										parse_int('/R' + numstr, tstr ),
										parse_int('/O' + numstr, tstr) );
						}
					}
			} else if(  copy(TStr, 1, 3) == '/G_') {
				goto do_globals;
			}
			Switch_Window(Status_Window);
			Down;
		}
	}
	while(  NOT( AT_EOF )  ) {

DO_GLOBALS:
		TStr = Copy(Get_Line,1,7);
		if(  TStr == '/G_STR='  ) {
			TStr = Copy(Get_Line,8,20);
			Down;
			Set_Global_Str(TStr,reconvert_string(Get_Line));
		} else if(  TStr == '/G_INT='  ) {
			TStr = Copy(Get_Line,8,20);
			Down;
			if(  Val(Temp_Integer, Get_Line) == 0  ) {
				Set_Global_Int(TStr,Temp_Integer);
			}
		}
		DOWN;
	}

	Call CLEAR_LINK_GLOBALS;

	Switch_Window(Status_Window);
	if(  (Window_Count > 1)  ) {	// 082493[scm]: change 2 to 1 to fix
																// the 'extra window' bug
/* This is a "just in case" thing so that if there are no /W= commands in
STATUS.ME, you will at least have one open window */
		Delete_Window;
		Switch_Win_Id(Active_Window);
	} else {
		Erase_Window;
		window_attr = $0;
	}

	if( !restore_mode )
	{
		set_global_int('@SCREEN_COUNT', t_sc );
	}

	RM('WINDOW^FindWin');
	set_global_int('MENU_LEVEL', 0 );
	set_global_int('SETUP_CHANGED', 0 );

	Refresh = True;
	New_Screen;
	if(!restore_mode)
		Make_Message('Previous status restored.');
	Goto END_OF_MAC;


CLEAR_LINK_GLOBALS:
  Temp_Integer = 1;
  while(  (Temp_Integer <= bid_count)  ) {
    Set_Global_Int('!Buffer_Id' + Str(global_int("!BID_" + str(temp_integer))),0);
    Set_Global_Int('!BID_' + str( temp_integer), 0);
		++ Temp_Integer;
	}
	RET;

END_OF_MAC:
	set_global_int("RESTORE", trestore );
	update_virtual_display;
	reset_virtual_display;
	Messages = True;
	return_int = active_window; // 4-05-93[scm] does sessmgr need the window?

}

macro STATUS TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	STATUS

Description: This macro saves the current status of the editor for the purpose
						 of restoring the editor to exactly the state it was in.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

int  JY,jx, Active_Window, Status_Window, Temp_Window, temp_backup ;
str  TStr[500], numstr[20];

	temp_backup = backups;
	backups = false;
	Undo_Stat = False;
	Refresh = False;
	Messages = False;
	Active_Window = Cur_Window;

/* Find the numerically highest window */
	Switch_Window(Window_Count);
/* Create a window to store all status data */
	Create_Window;
	Status_Window = Cur_Window;

	file_name = make_restr_name( global_int("RESTORE") ) + '.ME';

	Put_Line( '@MULTI-EDIT VERSION ' + remove_space(Copy(version,1,3))
			+ '    ' + fexpand( '' ));
	down;

	TStr = '/MISC=1' +
					'/I=' + Str(Insert_Mode) +
					'/V=' + Str(Video_Mode) +
					'/SC=' + Str( global_int('@SCREEN_COUNT' ) ) +
					'/EV=' + Str(Ext_Video_Mode) +
					'/CC=' + Str(  global_int('CUR_SCRN'));
	Put_Line(Tstr);
	Down;

	Switch_Window(1);
	while(  (Cur_Window < Status_Window)  ) {
		Temp_Window = Cur_Window;
		if(  (window_attr & $80) == 0  ) {
			TStr = '/W=' + Str(Cur_Window) +
							'/WNM=' + window_name +
							'/A=' + Str( Cur_Window == Active_Window ) +
							'/WA=' + Str( Window_Attr ) +
							'/FN=' + File_Name +
							'/C=' + Str(C_Col) +
							'/L=' + Str(C_Line) +
							'/R=' + Str(C_Row) +
							'/IL=' + Str(Indent_Level) +
							'/WS=' + Str(Wrap_Stat) +
							'/IS=' + Str(Indent_Style) +
							'/D=' + Str(Doc_Mode) +
							'/LS=' + Str(Link_Stat) +
							'/BI=' + Str(Buffer_Id) +
							'/BTML=' + Str(w_bottom_line) +
							'/RM=' + Str( right_margin ) +
              '/LT=' + convert_string(line_terminator) +
              '/FLL=' + str(fixed_line_length) +
							'/SN=' + str(screen_num);

			if(  (win_x1 != min_window_col) | (win_x2 != max_window_col) |
				 (win_y1 != min_window_row) | (win_y2 != max_window_row)  ) {
				tstr = tstr +
							'/X1=' + Str(Win_X1) +
							'/Y1=' + Str(Win_Y1) +
							'/X2=' + Str(Win_X2) +
							'/Y2=' + Str(Win_Y2);
			}
			if(  block_stat != 0  ) {
				tstr = tstr +
							'/BS=' + Str(Block_Stat) +
							'/BC1=' + Str(Block_Col1) +
							'/BC2=' + Str(Block_Col2) +
							'/BL1=' + Str(Block_Line1) +
							'/BL2=' + Str(Block_Line2);
			}
			if(  window_color_stat  ) {
				tstr = tstr + '/WCS=1/CTC=' + str(t_color ) +
												'/CCC=' + str( c_color ) +
												'/CBC=' + str( b_color ) +
												'/CHC=' + str( h_color ) +
												'/CSC=' + str( s_color ) +
												'/CLC=' + str( l_color ) +
												'/CLBC=' + str( lb_color ) +
												'/CEC=' + str( eof_color );
			}

			// Add in checking for the error windows
			if( global_int('~MEERR_ID') == window_id) {
				tstr = tstr + '/ERWN=1';
			}
			else if ( global_int('~MEERR_SPLIT_ID') == window_id) {
				tstr = tstr + '/ERSPLT=1';
			}
			/* This MUST be the last item!!!!! */
			if(  global_str('!WINZOOM#' + str(window_id)) != ''  ) {
				Tstr = tstr + '/ZOOM=' + global_str('!WINZOOM#' + str(window_id ) );
			}
			Switch_Window( Status_Window );
			Put_Line(Tstr);
			down;
			Switch_Window(temp_window);

			{		// here is where we save the position markers
				int jz, jk, line, position, row, offset;
				tstr = '';

				for( jk = 1; jk <= 3; ++jk)
					for( jz = 1; jz <= 10; ++jz ) {
						get_mark_record( jz, jk, line, position, row, offset );
						if( line > 0 ) {
							numstr = str(jz + (10 * (jk - 1))) + '=';
							tstr = tstr + '/L' + numstr + str( line ) +
													'/P' + numstr + str( position )+
													'/R' + numstr + str(  row ) +
													'/O' + numstr + str( offset );
						}
					}
				if( tstr != '') {
					jk = mark_stack_count;
					Switch_Window( Status_Window );
					Put_Line('/MARKS=' + str( jk ) + Tstr);
					down;
				}
			}
		}
		Switch_Window(Temp_Window + 1);
	}

/* Save some misc. general stuff at the end */
	Switch_Window(Status_Window);

	TStr =  First_Global( jx );
Loop:
	if(  (TStr != '')  ) {
		if(  (XPOS(str_char(tstr,1),'!@.~',1) == 0)  ) {
			if(  jx == 1  ) {
				Put_Line('/G_INT=' + TStr);  DOWN;
				Put_Line(Str(Global_int(tstr))); DOWN;
			} else {
				Put_Line('/G_STR=' + TStr);  DOWN;
				Put_Line(convert_string(Global_str(tstr))); DOWN;
			}
		}
		TStr = Next_Global(jx);
		Goto Loop;
	}

	Save_File;
	if(  parse_int('/NDEL=',mparm_str) == 0  ) {
		Delete_Window;
	}

	Switch_Window(Active_Window);

	Refresh = True;
	Messages = true;
	Undo_Stat = true;
	GOTO END_OF_MAC;



END_OF_MAC:
	backups = temp_backup;
}
