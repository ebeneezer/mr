macro_file WINDOW;

#INCLUDE WINDOW.SH
#INCLUDE MESYS.SH

#DEFINE SPLIT_MODE Global_Int( 'SPLIT_EDGE_MODE' )
#DEFINE AUTO_ARRANGE_ICONS Global_Int('AUTO_ARRANGE_ICONS')

/*******************************************************************************
												MULTI-EDIT MACRO FILE

NAME: WINDOW

This file contains all of the window oriented macros.

WINMENU				- The general purpose window list (menu) routine
PREVIEWWINDOW	- Implements preview window feature of WINMENU.
SELECT_WINDOW _ Takes the current window and makes it non hidden/minimized
FINDWIN				- If the current window is hidden, finds one that isn't
NEXTWIN				- Switches to the next non hidden window.
LASTWIN				- Switches to the preceding non hidden window.
ADJACENT_WIN	- Switches to an adjacent window
SCREEN_SHARE	- Routine used by ADJACENT_WIN.
SWITWIN				- The SWITCH WINDOW menu.  Uses WINMENU.
COPYBL				- Interwindow copy menu.  Uses WINMENU.
MOVEBL				- Interwindow move menu.  Uses WINMENU.
LINKWIN				- Link window menu.  Uses WINMENU.
DELWIN				- Delete window with verify.
MAKEWIN				- Creates a new window.
MOD_WIN				- Uses MOVE_WIN to modify the current window size
MOVE_WIN			- Moves or resizes windows.
ZOOM					- ZOOMs a window.
SPLITWIN			- Creates a new window and splits it vertically  or horizontally
WORGANIZE			- Organizes windows by tiling, cascading or icons.
WIN_OP				- Performs various window operations.
WIN_MENU_SET	- Sets up menu disable globals for the window menu.

										Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

macro WINMENU TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name: WINMENU

Description:	A general purpose window list (menu) routine

Parameters:
 /WT=<String>   The title of the top of the box.
 /WH=<String>   The "help" at the bottom of the box.
 /HS=<String>   The help index for HELP FILE if F1 is pressed.
 /HE=<Integer>  Enables user to hide windows, 0 or 1
 /DB=<Integer>  Causes display of only windows with marked blocks, 0 or 1.
 /DC=<Integer>  Causes display of the active window with the list, 0 or 1.
 Global_Int('@WINDOW_LIST_MODE@') If <> 0, Causes display format to have the
								window letter first so that the incremental search will key
								off of that instead of the file name.

 Returns:
								Return_Int = the number of the chosen window.

					Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int T_Refresh,
					Display_Current,Display_Blocked_Only,
					Active_Window,Menu_Window,
					Temp_Window,Start_Window,
					Menu_X,Menu_Y,
					Temp_Integer,Active_Line,Global_Hidden,
					Hide_Enable,Letter_First,
					Name_Col,Letter_Col,Attribute_Col,Path_Col,ID_Col,

					Start_Window_Id,
					Old_Window_ID,
					show_system_windows,
					Menu_Window_Id,
					Cur_Window_Id,
					menu = menu_create ;

		 str  Temp_String[128], attr_str[40] ,
					Help_Str[40] = Parse_Str('/HS=',MParm_Str),
					title[132] = Parse_Str('/WT=',MParm_Str),
					parm_str;

	Undo_Stat = False;
/*
		 if (Help_Str == '') {
		 Help_Str = 'WINLIST';
		 }
*/
	Letter_First = (Global_Int('@WINDOW_LIST_MODE@') != 0);
	Messages = False;
	T_Refresh = Refresh;
	Refresh = False;
	Hide_Enable = Parse_Int('/HE=',MParm_Str);
	Display_Current = Parse_Int('/DC=',MParm_Str);
	Display_Blocked_Only = Parse_Int('/DB=',MParm_Str);

	Menu_X = 1;
	Menu_Y = 3;
	Attribute_Col = 17;
	Path_Col = 41;
	Id_Col = 91;
	if (Letter_First) {
		Letter_Col = 1;
		Name_Col = 3;
	} else {
		Letter_Col = 14;
		Name_Col = 1;
	}
	if (Global_Hidden != 1) {
		Global_Hidden = False;
	}
	Active_Window = Window_Id;

/* Special parameter to show special system windows like MECONFIG.DB and
   the cut and paste buffer, etc.  Use at your own risk */
  show_system_windows = parse_int("/SYSTEM=",mparm_str);

	Start_Window_Id = Window_Id;
	Cur_Window_Id = Window_Id;

START_OVER:
/* Find the numerically highest window */
	Switch_Window(Window_Count);
	Error_Level = 0;
	Create_Window;
	if (Error_Level != 0) {
		Make_Message('Too many windows to run this macro!');
		Error_Level = 0;
		Goto ABORT;
	}
	Menu_Window = Cur_Window;
	Menu_Window_Id = Window_Id;
	File_Name = 'MEMENU.TMP';
	CALL NO_ERASE_LIST;

/* If there are no windows to display, then.... */
	if (C_Line < 2) {
		Return_Int = 0;
		Goto END_OF_MAC;
	}
	CALL Find_Cur_Window_Id;
	Ignore_Case = True;
	Active_Line = C_Line;

DO_MENU:

	Temp_Integer = 0;
	return_int = menu;
	RM("_DG_WIN_LIST");
	parm_str = return_str;
	if (svl(help_str)) {
		var_remove_str("/H=",parm_str);
		parm_str = "/H=" + help_str + parm_str;
	}
	if (svl(title)) {
		var_remove_str("/T=",parm_str);
		parm_str = "/T=" + title + parm_str;
	}
	temp_integer = 6;
	if (!hide_enable) {
		temp_integer = 2;
		menu_set_item(menu,1,
		menu_item_str(menu,5,1),
		menu_item_str(menu,5,2),
		menu_item_str(menu,5,3),
		menu_item_int(menu,5,1),
		menu_item_int(menu,5,2),
		menu_item_int(menu,5,3)
		);
		menu_set_item(menu,2,
		menu_item_str(menu,6,1),
		menu_item_str(menu,6,2),
		menu_item_str(menu,6,3),
		menu_item_int(menu,6,1),
		menu_item_int(menu,6,2),
		menu_item_int(menu,6,3)
		);
		var_remove_str("/#=",parm_str);
		parm_str = "/#=2" + parm_str;
	}
	menu_set_str(menu,temp_integer,3,"/WIN=" + str(cur_window) + menu_item_str(menu,temp_integer,3));
	return_int = menu;
	RM('USERIN^DATA_IN /A=2/HN=1/S=' + str(temp_integer) + parm_str);
	window_attr = $81;
	if (Return_Int < 2) {
		Goto MENU_EXIT;
	}

	if (Return_Int == 12) {
/* Delete window */
		Call Get_Cur_Window_Id;
		Old_Window_Id = Cur_Window_Id;
		Mark_Pos;
		Eof;
		if (C_Col == 1) {
			Up;
		}
		Return_Int = (C_Line > 1);
		Goto_Mark;
		Switch_Win_Id( Cur_Window_Id );
		if (Return_Int) {
//      RM('DELWIN');
      DELWIN(0,0,0);
		} else {
			RM('USERIN^CHECKFILE /H=WINDELETE');
			if (return_int <= 0) {
				Return_Int = 0;
			} else {
				Erase_Window;
			}
		}

		Switch_Win_Id( Menu_Window_Id );
		Menu_Window = Cur_Window;
		if (Return_Int != 0) {
			Active_Line = C_Line;
			CALL Rebuild_LIST;
			Goto_Line( Active_Line );
			Goto_Col(1);
			if (AT_EOF) {
				Up;
			}
			CALL Get_Cur_Window_Id;
			Active_Line = C_Line;
			if (old_Window_Id == Start_Window_Id) {
				Start_Window_Id = Cur_Window_Id;
			}
		}
		Goto DO_MENU;
	}

	if (Return_Int == 3) {
/* Save file in window */
		Active_Line = C_Line;
		CALL Get_Cur_Window_Id;
		Switch_Win_Id( Cur_Window_Id );
		RM( 'MEUTIL1^SAVEFILE /R=4/BC=' + str(box_count) );
		Switch_Window(Menu_Window);
		CALL Rebuild_List;
		Goto_Line( Active_Line );
		Goto DO_MENU;
	}

	if (Return_Int == 4) {
/* Hide a window */
		Active_Line = C_Line;
		CALL Get_Cur_Window_Id;
		Switch_Win_Id( Cur_Window_Id );
		Window_Attr ^= $01;
		CALL Rebuild_List;
		Goto_Line( Active_Line );
		Goto DO_MENU;
	}

	if (Return_Int == 5) {
		Temp_Integer = 1;
		while (Temp_Integer < Menu_Window) {
			Switch_Window( temp_integer );
			if ((Window_Attr & $80) == 0) {
				if (Global_Hidden) {
					Window_Attr = Window_Attr & $FE;
				} else {
					Window_Attr = Window_Attr | $01;
				}
			}
			++Temp_Integer;
		}
		Global_Hidden = Not(Global_Hidden);
		Switch_Window( Menu_Window );
		Active_Line = C_Line;
		CALL Rebuild_List;
		Goto_Line( Active_Line );
		GOTO Do_Menu;
	}

	if (Return_Int == 6) {
		Call PREVIEW;
	}

	Goto DO_MENU;

MENU_EXIT:
	if (Return_Int == 1) {
		CALL Get_Cur_Window_Id;

    if (Switch_Win_Id( Menu_Window_Id )) {
/* This is a quick and dirty fix for the following problem:
   If the all non-hidden windows are deleted from the list, a new window is
   created whose cur_window value is greater than the window used to make the
   list.  If we set Return_Int before we delete the list window, it will be
   the wrong value, so we are deleting it here.  I think the ultimate fix
   for this and the inefficient code in this macro is to rewrite it.  The
   macro BUF_LIST in BRIEF.S would be a good starting point for a rewrite.
*/
      delete_window;
    }

		Switch_Win_Id( Cur_Window_Id );
		Active_Window = Cur_Window_Id;
		Return_Int = Cur_Window;
	} else {
		Active_Window = Start_Window_Id;
	}
	Goto END_OF_MAC;

PREVIEW:
	CALL Get_Cur_Window_Id;
	Window_Attr = Window_Attr | $81;
	Switch_Win_Id(Cur_Window_Id);
	RM('PreviewWindow');
	if (RETURN_INT) {
		Cur_Window_Id = Window_Id;
	}
	Call Find_Cur_Window_Id;
	Active_Line = C_Line;
	RET;


REBUILD_LIST:
	Switch_Window( Menu_Window );
	Erase_Window;

No_Erase_List:
	Switch_Window(1);
	Working;
	do {
		Temp_Window = Cur_Window;
		if ( Window_Id != Menu_Window_Id )
		{
			if ((((Display_Current == True) || (Window_Id != Active_Window)) &
						((Display_Blocked_Only == False) || (Block_Stat != False)))
						&& (((window_attr & $80) == 0) || (show_system_windows))
						) {
				if( letter_first ) {
					temp_string = window_name + ' ';
					pad_str( Temp_String, name_col - 1, ' ' );
					Temp_String += Truncate_Path(File_Name) + ' ';
				}
				else
				{
					temp_string = Truncate_Path( file_Name ) + ' ';
					pad_str( Temp_String, letter_col - 1, ' ' );
					Temp_String += window_name + ' ';
				}
				pad_str( Temp_String, attribute_col - 1, ' ' );

				Attr_str = '';

				if (Read_Only) {
					Attr_str = 'ReadOnly ';
				} else if (File_Changed) {
					attr_str = 'Modified ';
				}
				if (Link_Stat) {
				 	attr_str = attr_str + 'Linked ';
				}
				if (Window_Attr & 0x80) {
				 	attr_str = attr_str + 'SYSTEM ';
				} else if (Window_Attr & $01) {
				 	attr_str = attr_str + 'Hidden ';
				}
				Temp_String += attr_str + ' ';
				pad_str( Temp_String, path_col - 1, ' ' );
				Temp_String += Get_Path(File_Name) + ' ';
				pad_str( Temp_String, id_col - 1, ' ' );
				Temp_String += str( window_id );

				Switch_Window(Menu_Window);
				Put_Line( Temp_String );
				Down;
			}
		}
		Switch_Window(Temp_Window + 1);
	} while ( Cur_Window!=1 );

	Switch_Window(Menu_Window);
	RET;


GET_Cur_Window_Id:
	Switch_Window( Menu_Window );
	VAL( Cur_Window_Id, Remove_Space(copy(get_line,Id_Col,100)) );
	RET;

FIND_Cur_Window_Id:
	Switch_Window( Menu_Window );
	TOF;
FCWN_LOOP:
	VAL( temp_integer, Shorten_Str(copy(get_line,Id_Col,4)) );
	if ((temp_integer != Cur_Window_Id) && NOT(AT_EOF)) {
		DOWN;
		Goto FCWN_LOOP;
	}
	if (at_eof) {
		tof;
	}
	RET;

END_OF_MAC:
	Refresh = False;

  if (Switch_Win_Id( Menu_Window_Id )) {
    Delete_Window;
  }
	New_SCREEN;

ABORT:
	Switch_Win_Id(Start_Window_Id);
	Refresh = T_Refresh;
	Messages = True;
	Undo_Stat = True;
exit:
	menu_delete(menu);
}

macro PreviewWindow TRANS {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:  PreviewWindow

Description:  Allows to preview windows before actually moving into them
			to edit.  Called only by WINMENU

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int t_attr;

	call UNHIDE;
	Refresh = True;
	Redraw;
	make_message('');
	Set_Global_Str('!WLEV#1', '/T=/KC=<PgUp>/W=6/K1=0/K2=73/R=1/Y=' + str(message_row) + '/X=' + str(message_col));
	Set_Global_Str('!WLEV#2', '/T=/KC=<PgDn>/W=6/K1=0/K2=81/R=2/Y=' + str(message_row) + '/X=' + str(message_col + 7));
	Set_Global_Str('!WLEV#3', '/T=LstWin/KC= /W=8/K1=0/K2=75/R=3/Y=' + str(message_row) + '/X=' + str(message_col + 14));
	Set_Global_Str('!WLEV#4', '/T=NxtWin/KC= |26/W=8/K1=0/K2=77/R=4/Y=' + str(message_row) + '/X=' + str(message_col + 23));
	Set_Global_Str('!WLEV#5', '/T=Select/KC=<ENTER>/W=13/K1=13/K2=28/R=5/Y=' + str(message_row) + '/X=' + str(message_col + 32));
	Set_Global_Str('!WLEV#6', '/T=Cancel/KC=<ESC>/W=11/K1=27/K2=1/R=6/Y=' + str(message_row) + '/X=' + str(message_col + 46));
	Set_Global_Str('!WLEV#7', '/T=Select/KC=<ENTER>/W=13/K1=13/K2=224/R=5/Y=' + str(message_row) + '/X=' + str(message_col + 32));

KEY_LOOP:
//		Make_Message('|26=switch window  <PgUp> <PgDn> <CtrlHome> <CtrlEnd> to browse.  <Enter>=select');
	RM('UserIn^CheckEvents /M=2/G=!WLEV#/#=7');
	Read_Key;
	if ((Key1 == 0) && (Key2 > 249)) {
		if (key2 == 250) {  /* process mouse event */
			if (Mou_Last_X == Win_X2) {
				RM('MOUSE^HandleScrollBar');
			} else {
				RM('Mouse^MouseInWindow');
				if (return_int) {
					RM('Mouse^MOUSE_MOVE /V=1/H=1');
				} else {
					RM('UserIn^CheckEvents /M=1/G=!WLEV#/#=7');
					if (return_int) {
						goto MOUSE_EVENT;
					}
				}
			}
			goto KEY_LOOP;
		} else if (Key2 == 251) {
			if (Mou_Last_X == Win_X2) {
				RM('MOUSE^GotoScrollBar');
				return_int = screen_num;
			} else {
				RM('Mouse^MouseInWindow');
				if (return_int) {
					RM('Mouse^MOUSE_MOVE /V=1/H=1');
				}
			}
			goto KEY_LOOP;
		}
	} else {
			RM('UserIn^CheckEvents /M=0/G=!WLEV#/#=7');
			if (return_int) {
MOUSE_EVENT:
				return_int = parse_int('/R=', return_str);
				if (return_int == 1) {
					Page_Up;
				} else if (return_int == 2) {
					Page_Down;
				} else if (return_int == 3) {
					call RESTORE_ATTR;
					RM('LASTWIN /HIDDEN=1');
					call UNHIDE;
					REDRAW;
				} else if (return_int == 4) {
					call RESTORE_ATTR;
					RM('NEXTWIN /HIDDEN=1');
					call UNHIDE;
					REDRAW;
				} else if (return_int == 5) {
					goto EXIT;
				} else if (return_int == 6) {
					goto EXIT;
				}
			}
		}

	goto KEY_LOOP;

UNHIDE:
	t_attr = window_attr;
	window_attr = 0;
	RET;

RESTORE_ATTR:
	window_attr = t_attr;
	RET;

EXIT:
	call RESTORE_ATTR;
	RM('UserIn^CheckEvents /M=3/F=1/G=!WLEV#/#=7');
	RETURN_INT = (return_int == 5);
	Refresh = False;
	Make_Message('');

}

/****************************Multi-Edit Macro********************************

 NAME:         Select_Window

 DESCRIPTION:  Takes the current window and makes it non-hidden and
							 non-minimized

 PARAMETERS:   None

 RETURNS:      Nothing

*****************************06-30-93 05:29pm*******************************/
macro Select_Window
{
	window_attr = window_attr & 0xFE;
	if(window_attr & 0x40)
	{
		rm("ZOOM");
	}
}

macro FINDWIN trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO
Name: FINDWIN

Description:	Finds the next non hidden window

					Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int JX;
	int
					no_min = parse_int('/NM=', mparm_str ),
					m = $81,
					error_win_id = Global_Int('~MEERR_ID')
					;

	if (parse_int("/HIDDEN=",mparm_str)) {
		m = $80;
	}
	if (no_min) {
		m = m | $40;
	}
again:
	JX = Cur_Window;
	while (((Window_Attr & m) != 0) || (window_id == error_win_id)) {
		if( window_id == error_win_id )
		{
			error_win_id = 0;
		}
		switch_window(cur_window + 1);
		if (jx == cur_window) {
			if (no_min) {
				m = $81;
				no_min = FALSE;
				goto again;
			}
			if ((window_attr & $80) == 0) {
				Window_Attr = Window_Attr & $FE;
				Redraw;
			}
			goto exit;
		}
	}

EXIT:

}

macro NEXTWIN TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO
Name: NEXTWIN

Description:	Switch to next window

					Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  tr, old_win, nw ;
	old_win = cur_window;
	TR = REFRESH;
	REFRESH = FALSE;
	SWITCH_WINDOW(CUR_WINDOW + 1);
	RM( 'FINDWIN /NM=1' + MParm_Str);
	if (cur_window == old_win)
			RM('FINDWIN');
	nw = cur_window;
	switch_window(old_win);
	REFRESH = TRUE;
	switch_window(nw);
	refresh = tr;
}

macro LASTWIN TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO
Name: LASTWIN

Description:	Switch to last window

									Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  tr, nw, ow, error_win_id = Global_Int('~MEERR_ID');
	TR = REFRESH;
	ow = cur_window;
	nw = cur_window;
	REFRESH = FALSE;
	SWITCH_WINDOW(CUR_WINDOW - 1);
	while ((cur_window != ow) && (((Window_Attr & $C1) != 0) || (window_id == error_win_id))) {
		switch_window(cur_window - 1);
	}
	if (cur_window ==  ow) {
		SWITCH_WINDOW(CUR_WINDOW - 1);
		while ((cur_window != ow) && (((Window_Attr & $81) != 0) || (window_id == error_win_id))) {
			switch_window(cur_window - 1);
		}
	}
	nw = cur_window;
EXIT:
	SWITCH_WINDOW( ow );
	REFRESH = TRUE;
	SWITCH_WINDOW( nw );
	REFRESH = TR;
}

macro ADJACENT_WIN TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO
Name: ADJACENT_WIN

Description:	On a split window, prompts for and switches to an adjacent
							window.

									Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Active_Window,New_Window,Active_Screen,Active_X1,Active_Y1,Active_X2,
					Active_Y2,T_Refresh,Common=0,T_Common=0,Ev_Count=0,T_Update_Time = 0;

	T_Refresh = Refresh;
	Refresh = False;
	Active_Window = Window_Id;
	Active_Screen = Screen_Num;
	New_Window = Active_Window;
	Active_X1 = Win_X1;
	Active_Y1 = Win_Y1;
	Active_X2 = Win_X2;
	Active_Y2 = Win_Y2;

	RM('SCREEN_SHARE');

	Put_Box(26,2, 55, 13, 0, m_b_color, 'Switch to window', true);

	if ((Return_Int & $7F) == 0) {
/* We stuck the checking of return_int from SCREEN_SHARE down here to minimize
the delay between the PUT_BOX and this message */
		RM('MEERROR^Beeps /C=1');
		Write('No adjacent windows',30,5,0,m_b_color);
		Write('to switch to!',33,6,0,m_b_color);
		Write('PRESS ANY KEY' ,34,10,0,m_b_color);
		Read_Key;
		Kill_Box;
		Goto EXIT;
	}

	if (Return_Int & 1) {
/* Enable up movement */
		++Ev_Count;
		Set_Global_Str('@WSEV#' + Str(Ev_Count), '/3D=1/T=/KC=  |30  /K1=0/K2=72/R=3/W=5/X=37/Y=4');
	}
	if (Return_Int & 2) {
/* Enable down movement */
		++Ev_Count;
		Set_Global_Str('@WSEV#' + Str(Ev_Count), '/3D=1/T=/KC=  |31  /K1=0/K2=80/R=4/W=5/X=37/Y=8');
	}
	if (Return_Int & 4) {
/* Enable left movement */
		++Ev_Count;
		Set_Global_Str('@WSEV#' + Str(Ev_Count), '/3D=1/T=/KC=  |17  /K1=0/K2=75/R=2/W=5/X=29/Y=6');
	}
	if (Return_Int & 8) {
/* Enable right movement */
		++Ev_Count;
		Set_Global_Str('@WSEV#' + Str(Ev_Count), '/3D=1/T=/KC=  |16  /K1=0/K2=77/R=1/W=5/X=44/Y=6');
	}
	++Ev_Count;
	Set_Global_Str('@WSEV#' + Str(Ev_Count), '/3D=1/T=Cancel/KC=<ESC>/K1=27/K2=1/R=0/W=11/X=35/Y=10');
	RM('USERIN^CheckEvents /M=2/G=@WSEV#/#=' + Str(Ev_Count));
	Read_Key;

	if ((key1 == 0) && (key2 == 250)) {
		RM('CheckEvents /G=@WSEV#/M=1/#=' + Str(Ev_Count));
	} else {
		RM('CheckEvents /G=@WSEV#/M=0/#=' + Str(Ev_Count));
	}
	Kill_Box;
	if (return_int != 0) {
		New_WIndow = Parse_Int('/' + Str(parse_int('/R=', return_str)) + '=',
									Global_Str('!SCREEN_SHARE'));
	}

EXIT:
	RM('USERIN^CheckEvents /M=3/G=@WSEV#/#=' + Str(Ev_Count));
	Set_Global_Str('!SCREEN_SHARE','');
	Refresh = TRUE;
	Switch_Win_Id(New_Window);
}

macro screen_share trans2 {
/*******************************************************************************
															MULTI-EDIT MACRO
Name: SCREEN_SHARE

Description:	Determines if the current window's screen is shared by another
							window.

Returns:
							Return_Int := 0, window is not shared
								OTHERWISE:
								Bit 0 = We can switch to window above
								Bit 1 = We can switch to window below
								Bit 2 = We can switch to window left
								Bit 3 = We can switch to window right
								BIT 7 = This is the ONLY available window

									Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int upw = 0, downw = 0, leftw = 0, rightw = 0,
			upt = 0, downt = 0, leftt = 0, rightt = 0,
			upl = 0, downl = 0, leftl = 0, rightl = 0,
			ups = 0, downs = 0, lefts = 0, rights = 0,
			visible_count = 0,
			sn = screen_num,
			jx = 0,
			jy,
			tr = refresh,
			tw = cur_window,
			x1 = win_x1, x2 = win_x2, y1 = win_y1, y2 = win_y2
			;

	refresh = false;
	return_int = 0;

	for (jx = 1; jx <= window_count; jx++) {
		if (jx != tw) {
			switch_window( jx );
			if (!(window_attr & 0x81)) {
				++visible_count;
				if (((x1 == win_x2) || (x1 == (win_x2 + 1))) &&
						(win_y2 > y1) && (win_y1 < y2)) {
					call calc_y;
					if (screen_num == sn) {
						if ((jy > leftt) || (lefts != sn))
								call set_left;
					} else if (last_update_time > leftl) {
						call set_left;
					}
				} else if (((x2 == win_x1) || (x2 == (win_x1 - 1))) &&
						(win_y2 > y1) && (win_y1 < y2)) {
					call calc_y;
					if (screen_num == sn) {
						if ((jy > rightt) || (rights != sn))
								call set_right;
					} else if (last_update_time > rightl) {
						call set_right;
					}
				} else if (((y2 == win_y1) || (y2 == (win_y1 - 1))) &&
						(win_x2 > x1) && (win_x1 < x2)) {
					call calc_x;
					if (screen_num == sn) {
						if ((jy > downt) || (downs != sn))
								call set_down;
					} else if (last_update_time > downl) {
						call set_down;
					}
				} else if (((y1 == win_y2) || (y1 == (win_y2 + 1))) &&
						(win_x2 > x1) && (win_x1 < x2)) {
					call calc_x;
					if (screen_num == sn) {
						if ((jy > upt) || (ups != sn))
								call set_up;
					} else if (last_update_time > upl) {
						call set_up;
					}
				}
			}
		}
	}

	switch_window( tw );
	if (upw)
			return_int |= 0x01;
	if (downw)
			return_int |= 0x02;
	if (leftw)
			return_int |= 0x04;
	if (rightw)
			return_int |= 0x08;
	if (visible_count == 0)
			return_int |= 0x80;
	Set_Global_Str('!SCREEN_SHARE',
									'/3=' + Str(UpW) +
									'/4=' + Str(DownW) +
									'/2=' + Str(LeftW) +
									'/1=' + Str(RightW)
									);
	goto exit;

calc_y: {
	if (win_y2 <  y2) {
		if (win_y1 > y1) {
			jy = win_y2 - win_y1 + 1;
		} else {
			jy = win_y2 - y1 + 1;
		}
	} else {
		if (y1 > win_y1) {
			jy = y2 - y1 + 1;
		} else {
			jy = y2 - win_y1 + 1;
		}
	}
	ret;
}

calc_x: {
	if (win_x2 <  x2) {
		if (win_x1 > x1) {
			jy = win_x2 - win_x1 + 1;
		} else {
			jy = win_x2 - x1 + 1;
		}
	} else {
		if (x1 > win_x1) {
			jy = x2 - x1 + 1;
		} else {
			jy = x2 - win_x1 + 1;
		}
	}
	ret;
}

set_left: {
	leftt = jy;
	leftw = window_id;
	leftl = last_update_time;
	lefts = screen_num;
	ret;
}

set_right: {
	rightt = jy;
	rightw = window_id;
	rightl = last_update_time;
	rights = screen_num;
	ret;
}

set_up: {
	upt = jy;
	upw = window_id;
	upl = last_update_time;
	ups = screen_num;
	ret;
}

set_down: {
	downt = jy;
	downw = window_id;
	downl = last_update_time;
	downs = screen_num;
	ret;
}

exit:
	refresh = tr;
}

macro SWITWIN TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO
Name: SWITWIN

Description:	Brings up a menu of all windows for the purpose of switching
							directly to a new window without toggling through those in
							between

								Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	RM( 'WINMENU /WT=WINDOW LIST/WH=Press <ENTER> to select window, <ESC> to return to original window./HS=WINLIST/HE=1/DB=0/DC=1' + MParm_Str);
	if (Return_Int != 0) {
		Switch_Window(Return_Int);
    RM("SELECT_WINDOW");
//    if ( Parse_Int('/ZOOM=',MParm_Str ) )
//      RM ('ZOOM');
	}
//  Window_Attr = Window_Attr & $FE;
	redraw;
}

macro COPYBL TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO
Name: COPYBL

Description:	Brings up a menu of all windows who have a block defined for
							inter-window block copying

								Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	RM( 'WINMENU /WT=INTER-WINDOW COPY/WH=Select window to copy from and press <ENTER>, or <ESC> to abort/HS=BLOCKWIN/HE=0/DB=1/DC=0' + MParm_Str);
	Window_Attr = Window_Attr & $FE;
	if (Return_Int != 0) {
		Window_Copy(Return_Int);
	}
}

macro MOVEBL TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO
Name: MOVEBL

Description:	Brings up a menu of all windows who have a block defined for
							inter-window block moving

								Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	RM( 'WINMENU /WT=INTER-WINDOW MOVE/WH=Select window to move from and press <ENTER>, or <ESC> to abort/HS=BLOCKWIN/HE=0/DB=1/DC=0' + MParm_Str);
	Window_Attr = Window_Attr & $FE;
	if (Return_Int != 0) {
		Window_Move(Return_Int);
	}
}

macro LINKWIN TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO
Name: LINKWIN

Description:	Brings up a menu of all windows for linking

Parameters:
							/X=		X coordinate for the menu box
							/Y=		Y coordinate for the menu box
							/BC=	Number of boxes to kill upon exit

								Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int jx, mr;

	MR = Parse_Int('/Y=',MParm_Str);
	if (MR == 0) {
		MR = 2;
	}
	if (link_stat == 0) {
		if (global_str('@DEL_VERIFY_MACRO') != '') {
			rm( global_str('@DEL_VERIFY_MACRO'));
		}
		if (File_Changed) {
			RM('USERIN^CHECKFILE /H=WINLINK/Y=' + str(mr) + '/X=' +
					parse_str('/X=',mparm_str));
			if (return_int <= 0) {
				Return_Int = 0;
				GOTO Exit;
			}
		}
	}

	jx = Parse_Int('/BC=',mparm_str);
	while (box_count > jx) {
		kill_box;
	}

	RM( 'WINMENU /WT=LINK WINDOW/WH=Select window to link to and press <ENTER>, or <ESC> to abort/HS=WINLINK/HE=0/DB=0/DC=0' + MParm_Str);
	if (Return_Int != 0) {
		Link_Window(Return_Int);
		Return_Int = 1;
		RM('SetWindowNames');
	}
	Window_Attr = Window_Attr & $FE;

	return_int = 100;
EXIT:

}

int DELWIN(int no_del = parse_int("/NODEL=", mparm_str),
            int MR = parse_int("/Y=",mparm_str),
            int BC = parse_int("/BC=",mparm_str)) TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO
Name: DELWIN

Description:	Deletes a window after checking to see if file has been saved

Parameters:
							/BC=	Number of boxes leave upon exit
							/NODEL=1  Perform all operations as if the window was
												deleted (like resizing other windows) but do not
												actually delete the window.

								Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  int jx, /*mr,*/ x1,y1, x2,y2, ts, new_win, return_win, new_ts, tr;
  int  w_id, mx1, mx2, my1, my2/*,
          no_del = parse_int('/NODEL=', mparm_str)*/
					;

	Return_Int = False;
	tr = refresh;
//  MR = Parse_Int('/Y=',MParm_Str);
	if (MR == 0) {
		MR = 2;
	}
	new_win = 0;

	if (!( no_del ) && (link_stat == 0)) {
		RM('USERIN^CHECKFILE /H=WINDELETE');
		if (return_int <= 0) {
			return_int = false;
			GOTO Exit;
		}
	}
//  BC = Parse_Int('/BC=',mparm_str);
  while (box_count > BC) {
		kill_box;
	}
	Return_Int = True;
	WORKING;
	refresh = FALSE;
	if ( !no_del && (window_id == global_int('~MEERR_SPLIT_ID')) ) {
		jx = window_id;
		if( switch_win_id( global_int('~MEERR_ID' ) ) ) {
			if( !file_changed ) {
//        RM('DELWIN');
        DELWIN(0,0,0);
			}
			switch_win_id( jx );
		}
	}


/* THIS BLOCK OF CODE CAUSES PROBLEMS AND DOESN'T APPEAR TO BE NECESSARY.
   IF WE PUT IT BACK IN, WE'VE GOT TO FIX IT!
  if ((global_str('!WINZOOM#' + str(window_id)) != '') &&
      (!parse_int("/NOZOOM=",Mparm_Str))) {
		RM('ZOOM');
	}
*/

	x1 = win_x1;
	y1 = win_y1;
	x2 = win_x2;
	y2 = win_y2;
	mx1 = screen_width;
	mx2 = 0;
	my1 = screen_length;
	my2 = 0;
	ts = screen_num;
	w_id = window_id;
	Refresh = False;

/* If the window we are about to delete shares the screen with another, choose
   the first shared window we find, otherwise choose according to sequence

  RM("SCREEN_SHARE");
  if (return_int & 0x7F) {
    switch (return_int) {
      case 1 :
        return_int = 3;
        break;
      case 2 :
        return_int = 4;
        break;
      case 4 :
        return_int = 2;
        break;
      case 8 :
        return_int = 1;
        break;
    }
    New_Win = Parse_Int('/' + Str(return_int) + '=',
              Global_Str('!SCREEN_SHARE'));
  } else {
 */
    if (Cur_Window == Window_Count) {
      switch_window( cur_window - 1);
      new_win = window_id;
      switch_window( cur_window + 1);
    } else {
      switch_window( cur_window + 1);
      new_win = window_id;
      switch_window( cur_window - 1);
    }
    jx = window_id;
    rm('FINDWIN /NM=1');
    if (new_win == cur_window) {
/*
RM("BEEPS /C=3");
*/
      rm('FINDWIN');
    }
    return_win = window_id;
    switch_win_id(jx);
/*
  }
*/

	if (no_del == 0) {
		set_global_str('!WINZOOM#' + str(window_id), '');
		DELETE_WINDOW;
	}
	new_ts = screen_num;
	jx = 0;
	refresh = false;
	if (ts != 0) {
		jx = 0;
		while (jx < window_count) {
			++jx;
				switch_window(jx);
				if ((screen_num == ts) && (window_id != w_id) && ((window_attr & $C1) == 0)) {
					if (((y2 == win_y1) || (y2 == (win_y1 - 1))) && (x1 <= win_x1) && (x2 >= win_x2)) {
						new_win = window_id;
						size_window( win_x1, y1, win_x2, win_y2);
						if (x1 == win_x1) {
							x1 = win_x2;
						}
						if (x2 == win_x2) {
							x2 = win_x1;
						}
						if (x2 < x1) {
							x2 = 0;
							x1 = 0;
							y2 = y1;
						}
					}
					if (((y1 == win_y2) || (y1 == (win_y2 + 1))) && (x1 <= win_x1) && (x2 >= win_x2)) {
						new_win = window_id;
						size_window( win_x1, win_y1, win_x2, y2);
						if (x1 == win_x1) {
							x1 = win_x2;
						}
						if (x2 == win_x2) {
							x2 = win_x1;
						}
						if (x2 < x1) {
							x2 = 0;
							x1 = 0;
							y1 = y2;
						}
					}
					if (((x1 == win_x2) || (x1 == (win_x2 + 1))) && (y1 <= win_y1) && (y2 >= win_y2)) {
						new_win = window_id;
						size_window( win_x1, win_y1, x2, win_y2);
						if (y1 == win_y1) {
							y1 = win_y2;
						}
						if (y2 == win_y2) {
							y2 = win_y1;
						}
						if (y2 < y1) {
							y2 = 0;
							y1 = 0;
							x1 = x2;
						}
					}
					if (((x2 == win_x1) || (x2 ==( win_x1 - 1))) && (y1 <= win_y1) && (y2 >= win_y2)) {
						new_win = window_id;
						size_window( x1, win_y1, win_x2, win_y2);
						if (y1 == win_y1) {
							y1 = win_y2;
						}
						if (y2 == win_y2) {
							y2 = win_y1;
						}
						if (y2 < y1) {
							y2 = 0;
							y1 = 0;
							x2 = x1;
						}
					}
					if (win_x1 < mx1) {
						mx1 = win_x1;
					}
					if (win_x2 > mx2) {
						mx2 = win_x2;
					}
					if (win_y1 < my1) {
						my1 = win_y1;
					}
					if (win_y2 > my2) {
						my2 = win_y2;
					}
				}
		}
	/*  jx := 0;
		while (jx < window_count) do
			++jx;
			switch_window(jx);
			if (screen_num = ts) then
				 w_bottom_line := (win_y2 >= my2);
			end;
		end;
		*/
	}

out:
	if (no_del) {
		switch_win_id( w_id );
	} else {
    switch_win_id(new_win);
	}

  RM( 'FINDWIN /NM=1' );
  if ((window_attr & $80) != 0) {
    RM('CREATEWINDOW');
    RM('EXTSETUP /PRE=1');
  }
  RM( 'SetWindowNames');
  New_Screen;
	if (auto_arrange_icons)
//    rm('WIN_ICON_ARRANGE /DELETE=1');
    WIN_ICON_ARRANGE(1);
//  return_int = 100;
  return_int = new_win;
	refresh = TR;
EXIT:

set_global_int("!T_WIN!", return_win);


  RETURN(new_win);
}

void WIN_ICON_ARRANGE(int delete_flag =
                      parse_int("/DELETE=",mparm_str)) trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: WIN_ICON_ARRANGE

Description:  Arranges window icons at the bottom of the screen.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int 	jy,
//        ty = max_window_row - (max_window_row == fkey_row) + 1,
        ty = max_window_row + 1,
				tx = screen_width + 1,
				t_id = window_id,
				tt = 0,
        nl;

  if (ty > (screen_length + 1)) {
    ty = (screen_length + 1);
  }

  for (jy = 1; jy <= window_count; jy++) {
		switch_window( jy );
		if ((window_attr & $40) != 0) {
			nl = length( truncate_path( file_name ) ) + 1;
			if ((tx + nl) >= screen_width) {
				tx = 1;
				--ty;
				++tt;
			}
			size_window( tx, ty, tx + nl, ty + 1);
			tx += 16;
		}
	}
//  RM('SetScrn /ICON=1/SIROW=1/IROWS=' + str(tt) +
//      "/DELETE=" + parse_str("/DELETE=",mparm_str));
//  RM('SetScrn /ICON=1/SIROW=1/IROWS=' + str(tt) +
//      "/DELETE=" + str(delete_flag));
  SETSCRN(1,tt,1,"",0,0,delete_flag);
	switch_win_id( t_id );
  RETURN();
}

macro MAKEWIN TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO
Name: MAKEWIN

Description:	Creates a new window after checking to see if the window limit
							has been reached

Parameters:
							/NL=1		Will not load a file or attempt a link.  Use this if you
											just want to create a window with a window name and you'll
											take care of loading a file yourself.

							/L=1		Will cause the new window to be linked to the
											original window if the file is already loaded AND
											the user has requested the file NOT be reloaded.

							/NC=1		Will bypass the creation of the window, and just prompt for
											loading a file and taking care of linking if the file is already
											loaded.

								Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  tw1, tw2, jx,nc ;

	if (Window_Count >= 100) {
		Error_Level = 1001;
		Goto Exit;
	}
	nc = parse_int('/NC=', mparm_str);
	refresh = FALSE;
	if (nc == 0) {
		tw2 = window_id;
		RM('CreateWindow');
		tw1 = cur_window;
		switch_win_id( tw2);
		refresh = true;
		switch_window( tw1 );
		tw1 = window_id;
		RM('EXTSETUP /PRE=1');
	} else {
		tw1 = window_id;
	}
	if (Parse_Int('/NL=',MParm_Str)) {
		Goto NL_EXIT;
	}
Bypass_Create:
	RM('MEUTIL1^LOADFILE /X=' + Str(win_x1) + '/Y=' + str(win_y1) + '/E=' + Str(nc == 0));
	tw2 = window_id;
	if (tw2 != tw1) {
		refresh = false;
		jx = cur_window;
		if (switch_win_id( tw1 )) {
			if (caps(file_name) == '?NO-FILE?') {
				if (parse_int('/L=',mparm_str)) {
					link_window( jx );
					make_message('Window linked.');
				} else if (nc == 0) {
					delete_window;
					new_screen;
					if (switch_win_id( tw2 )) {
					}
				}
			}
		}
	}
NL_EXIT:
	RM('SetWindowNames');
Exit:
}

void MOD_WIN(int move_mode = parse_int("/MM=",mparm_str),
            int mouse_move = parse_int("/MS=",mparm_str),
            int no_prompt = parse_int("/NOPROMPT=",mparm_str)) TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

NAME:  MOD_WIN

DESCRIPTION:  Replaces the MODIFY_WINDOW macro command.

						/MS=		Enables mouse support
						/MM=		If 0, resize window.  If 1, move window.
            /NOPROMPT= If 1, bypasses interactive resizing.  Coordinates are
                       expected to be in Return_Str as follows:
                        /X1=nn/Y1=nn/X2=nn/Y2=nn

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  x1, x2, y1, y2, jx, old_refresh, tw, ts ;
  int  nx1, nx2, ny1, ny2, w_id, jx/*, mouse_move, mm*/;

  int  ux1, ux2, uy1, uy2, /*move_mode,*/ screen_share_count/*,
      no_prompt = parse_int("/NOPROMPT=",Mparm_Str)*/;
	str  sx1[20], sx2[20], sy1[20], sy2[20] ;

	ts = screen_num;
	old_refresh = refresh;
	refresh = false;
	tw = cur_window;
  if (no_prompt) {
    x1 = Parse_Int('/X1=',Return_Str);
    y1 = Parse_Int('/Y1=',Return_Str);
    x2 = Parse_Int('/X2=',Return_Str);
    y2 = Parse_Int('/Y2=',Return_Str);
  } else {
    x1 = win_x1;
    y1 = win_y1;
    x2 = win_x2;
    y2 = win_y2;
  }


	ux1 = 0;
	ux2 = 0;
	uy1 = 0;
	uy2 = 0;
	sx1 = '';
	sx2 = '';
	sy1 = '';
	sy2 = '';
//  mouse_move = Parse_Int('/MS=', mparm_str);
//  move_mode = Parse_Int('/MM=', mparm_str);

	screen_share_count = 0;
	jx = 0;
  if (screen_num != 0) {
		while (jx < window_count) {
			++jx;
			switch_window(jx);
      if ((screen_num == ts) && (cur_window != tw) &&
          (!(window_attr & 0x40))) {
//      if (screen_num == ts) {
				++screen_share_count;
//        if ((win_x2 == x1) || (win_x2 == (x1 - 1))) {
        if (win_x2 >= (x1 - 1)) {
					if ((win_y1 < y2) && (win_y2 >  y1)) {
						if ((win_y1 < y1) || (win_y2 > y2)) {
							ux1 = 1;
						} else {
							sx1 = sx1 + char(cur_window);
						}
					}
				}
//        if ((win_x1 == x2) || (win_x1 == (x2 + 1))) {
        if (win_x1 <= (x2 + 1)) {
					if ((win_y1 < y2) && (win_y2 >  y1)) {
						if ((win_y1 < y1) || (win_y2 > y2)) {
							ux2 = 1;
						} else {
							sx2 = sx2 + char(cur_window);
						}
					}
				}
//        if ((win_y2 == y1) || (win_y2 == (y1 - 1))) {
        if (win_y2 >= (y1 - 1)) {
					if ((win_x1 < x2) & (win_x2 >  x1)) {
						if ((win_x1 < x1) | (win_x2 > x2)) {
							uy1 = 1;
						} else {
							sy1 = sy1 + char(cur_window);
						}
					}
				}
//        if ((win_y1 == y2) || (win_y1 == (y2 + 1))) {
        if (win_y1 <= (y2 + 1)) {
					if ((win_x1 < x2) & (win_x2 >  x1)) {
						if ((win_x1 < x1) | (win_x2 > x2)) {
							uy2 = 1;
						} else {
							sy2 = sy2 + char(cur_window);
						}
					}
				}
			}
		}
	}

	switch_window(tw);

	if ((sx1 != '') || (sx2 != '') || (Sy1 != '') || (sy2 != '')) {
		ux1 = (sx1 == '');
		ux2 = (sx2 == '');
		uy1 = (sy1 == '');
		uy2 = (sy2 == '');
		if (uy1 ^ uy2) {
			uy1 = 0;
			uy2 = 0;
		}
		if (ux1 ^ ux2) {
			ux1 = 0;
			ux2 = 0;
		}

	}

  if (!no_prompt) {
    RM('MOVE_WIN /M=1/X1=' + str(win_x1)+'/Y1=' + Str(win_y1) + '/X2=' +
      Str(win_x2) + '/Y2=' + Str(win_y2) + '/MS=' + str(mouse_move) +
      '/MX1=' + str( (Window_Attr & $40) != 0)  + '/MY1=' + Str(Min_Window_Row) + '/MX2=' +
      Str(Screen_Width + ((Window_Attr & $40) == 0)) + '/MY2=' + Str(Max_Window_Row + ((window_attr & $40) != 0)) +
      '/MM=' + str(mouse_move) +
      '/UX1=' + str(ux1) +
      '/UX2=' + str(ux2) +
      '/UY1=' + str(uy1) +
      '/UY2=' + str(uy2) + '/K=' + str((move_mode == 0))
      );
  }
		return_int = 1;
	refresh = false;
	Size_Window(Parse_Int('/X1=',Return_Str),Parse_Int('/Y1=',Return_Str),
		Parse_Int('/X2=',Return_Str),Parse_Int('/Y2=',Return_Str));

	while (svl(sx1) > 0) {
		switch_window( ascii(copy(sx1,1,1)));
		sx1 = str_del(sx1,1,1);
		size_window(win_x1,win_y1,Parse_Int('/X1=', return_str) - split_mode, win_y2);
	}
	while (svl(sx2) > 0) {
		switch_window( ascii(copy(sx2,1,1)));
		sx2 = str_del(sx2,1,1);
		size_window(Parse_Int('/X2=', return_str) + split_mode,win_y1,win_x2, win_y2);
	}
	while (svl(sy1) > 0) {
		switch_window( ascii(copy(sy1,1,1)));
		sy1 = str_del(sy1,1,1);
		size_window(win_x1,win_y1,win_x2, Parse_Int('/Y1=',return_str) - split_mode);
	}
	while (svl(sy2) > 0) {
		switch_window( ascii(copy(sy2,1,1)));
		sy2 = str_del(sy2,1,1);
		size_window(win_x1,Parse_Int('/Y2=', return_str) + split_mode,win_x2, win_y2);
	}
 /* RM('setbtms /SN=' + str(ts)); */
	switch_window(tw);
  refresh = old_refresh;
//  if (NOT( virtual_display )) {
		New_Screen;
//  }
  RETURN();
}

macro MOVE_WIN TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: Move_Win

Description: Used for moving and resizing a window or box.

Paramters:  /X1=		Current upper left column
						/Y1=		Current upper left row
						/X2=		Current lower right column
						/Y2=		Current lower right row
						/MX1=		Mininum upper left column
						/MX2=		Maximum upper left column
						/MY1=		Mininum lower right row
						/MY2=		Maximum lower right row
						/UX1=		If 1, inhibits movement of top window border
						/UX2=		If 1, inhibits movement of bottom window border
						/UY1=		If 1, inhibits movement of left window border
						/UY2=		If 1, inhibits movement of right window border
						/MM=		0 = Move whole window
												1 = Move upper left corner
												2 = Move upper right corner
												3 = Move lower left corner
												4 = Move lower right cornter
						/M=1		Use virtual screen moves.  This is available ONLY when
										sizing a text window in EDIT MODE.
							 2    Use virtual screen to move PUT_BOX.
						/K=1		Use keyboard mode

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  x1, x2, y1, y2, min_x, max_x, min_y, max_y,
					ox1, oy1, ox2, oy2, mx, my, o_bl, bc, jx,
					w, h, t_int, old_attr, move_count, mmode, t_s_color, t_b_color,
					shift_stat,
/* Abs_Max_Y takes into account window ICONS which can only be allowed to move
to the row above Fkey_Row */
					Abs_Max_Y = Screen_Length + 1 -
											(((window_attr & $40) != 0) &
												(Fkey_Row == Screen_Length));
	int  mou_orig_x, mou_orig_y, mou_orig_x2, mou_orig_y2, k1, k2,
					mm, ux1, ux2, uy1, uy2, min_width, min_height, use_key ;

/*  set_global_int('MENU_LEVEL', global_int('MENU_LEVEL') + 1); */

	mmode = parse_int('/M=', mparm_str );
	if (mmode == 1) {
		if (NOT(virtual_display) || (mode != EDIT)) {
			mmode = 0;
		}
	} else if ((mmode == 2) && (virtual_display == 0)) {
		mmode = 0;
	}
	use_key = parse_int('/K=', mparm_str );

	Min_X = Parse_Int('/MX1=',MParm_Str);
	if ((Min_X < 0) || (Min_X > (Screen_Width + 1))) {
		Min_X = 0;
	}
	Min_Y = Parse_Int('/MY1=',MParm_Str);
	if ((Min_Y < 0) || (Min_Y > (Screen_Length + 1))) {
		Min_Y = 0;
	}
	Max_X = Parse_Int('/MX2=',MParm_Str);
	if ((Max_X < 1) | (Max_X > (Screen_Width + 1))) {
		Max_X = Screen_Width + 1;
	}
	Max_Y = Parse_Int('/MY2=',MParm_Str);
	if ((Max_Y < 1) | (Max_Y > Abs_Max_Y)) {
		Max_Y = Abs_Max_Y;
/*
	IF ((Max_Y < 1) or (Max_Y > (Screen_Length + 1))) THEN
		Max_Y := Screen_Length + 1;
 */
	}

	X1 = Parse_Int('/X1=',MParm_Str);
	if ((X1 < Min_X) | (X1 > (Max_X - 1))) {
		X1 = Min_X;
	}
	Y1 = Parse_Int('/Y1=',MParm_Str);
	if ((Y1 < Min_Y) | (Y1 > (Max_Y - 1))) {
		Y1 = Min_Y;
	}
	X2 = Parse_Int('/X2=',MParm_Str);
	if ((X2 < (Min_X + 1)) | (X2 > Max_X)) {
		X2 = Max_X;
	}
	Y2 = Parse_Int('/Y2=',MParm_Str);
	if ((Y2 < (Min_Y + 1)) | (Y2 > Max_Y)) {
		Y2 = Max_Y;
	}

	MIN_HEIGHT = parse_int('/H=', mparm_str );
	if (min_height == 0) {
		min_height = 1;
	}
	MIN_WIDTH = parse_int('/W=', mparm_str );
	if (min_width == 0) {
		min_width = 2;
	}

	ox1 = x1;
	oy1 = y1;
	ox2 = x2;
	oy2 = y2;

	ux1 = Parse_Int( '/UX1=', mparm_str);
	ux2 = Parse_Int( '/UX2=', mparm_str);
	uy1 = Parse_Int( '/UY1=', mparm_str);
	uy2 = Parse_Int( '/UY2=', mparm_str);


	mm = Parse_Int('/MM=', mparm_str);

	w = x2 - x1;
	h = y2 - y1;
	if (!(use_key)) {
		mou_orig_x = mou_last_x - x1;
		mou_orig_y = mou_last_y - y1;
		mou_orig_x2 = x2 - mou_last_x;
		mou_orig_y2 = y2 - mou_last_y;
		if (mm == 0) {
			Mou_Set_Limits(min_x + mou_orig_x,min_y,max_x - w + mou_orig_x, max_y - h);
		} else if (mm == 1) {
			ux2 = 1;
			uy2 = 1;
		} else if (mm == 2) {
			ux1 = 1;
			uy2 = 1;
		} else if (mm == 3) {
			ux2 = 1;
			uy1 = 1;
		} else if (mm == 4) {
			ux1 = 1;
			uy1 = 1;
		}
	}
	t_int = box_count;

	if (mmode == 1) {
		set_virtual_display;
		working;
		old_attr = window_attr;
		window_attr = 1;
		o_bl = w_bottom_line;
		w_bottom_line = true;
		refresh = true;
		new_screen;
		override_screen_seg;
		save_box( 1, 1, screen_width, screen_length );
		reset_screen_seg;
		Clear_Virtual_Display;
		t_b_color = b_color;
		t_s_color = s_color;
		s_color = error_color;
		b_color = error_color;
		redraw;
		window_attr = old_attr;
		update_virtual_display;
	} else if (mmode == 2) {
		gotoxy( 0, 0 );
		clear_virtual_display;
		call draw_error_outline;
		set_virtual_display;
		save_box( 1, 1, screen_width, screen_length);
		kill_box;
		bc = box_count;
		save_box( x1, y1, x2, y2 );
		restore_box_num( box_count - 1 );
		override_screen_seg;
		save_box( 1, 1, screen_width, screen_length);
		reset_screen_seg;
	} else {
		set_virtual_display;
		update_status_line;
		save_box( 1, 1, screen_width, screen_length );
	}

	if (use_key) {
		goto key_loop;
	}
move_loop:
	call resize;
move_loop2:
	mx = mou_last_x;
	my = mou_last_y;
	Mou_Check_Status;
	if (Mou_Last_Status & 1) {
		if ((mou_last_x != mx) | (mou_last_y != my)) {
			if (mm == 0) {
				if (((X1 + mou_orig_x) != Mou_Last_X) | (Y1 != Mou_Last_Y)) {
					X1 = Mou_Last_X - mou_orig_x;
					Y1 = Mou_Last_Y;
					x2 = x1 + w;
					y2 = y1 + h;
					goto move_loop;
				}
			} else if (mm == 1) {
				if (((X1 + mou_orig_x) != Mou_Last_X) | ((Y1 + mou_orig_y) != Mou_Last_Y)) {
					X1 = Mou_Last_X - mou_orig_x;
					Y1 = Mou_Last_Y - mou_orig_y;
					call clip_x1;
					call clip_y1;
					goto move_loop;
				}
			} else if (mm == 2) {
				if (((X2 - mou_orig_x2) != Mou_Last_X) | ((Y1 + mou_orig_y) != Mou_Last_Y)) {
					X2 = Mou_Last_X + mou_orig_x2;
					Y1 = Mou_Last_Y - mou_orig_y;
					call clip_x2;
					call clip_y1;
					goto move_loop;
				}
			} else if (mm == 3) {
				if (((X1 + mou_orig_x) != Mou_Last_X) | ((Y2 - mou_orig_y2) != Mou_Last_Y)) {
					X1 = Mou_Last_X - mou_orig_x;
					Y2 = mou_orig_y2 + mou_last_y;
					call clip_x1;
					call clip_y2;
					goto move_loop;
				}
			} else if (mm == 4) {
				if (((X2 - mou_orig_x2) != Mou_Last_X) | ((Y2 - mou_orig_y2) != Mou_Last_Y)) {
					X2 = Mou_Last_X + mou_orig_x2;
					Y2 = mou_orig_y2 + mou_last_y;
					call clip_x2;
					call clip_y2;
					goto move_loop;
				}
			}
		}
		goto move_loop2;
	}
	goto move_done;


key_loop:
	call resize;
key_loop2:
loopxxx:
	read_key;
	K1 = Key1;
	K2 = Key2;

/* This is to take care of key type ahead filling up the buffer */
	if(key_macro_num == 0)
	{
		if (Check_Key) {
			if ((key1 == k1) && (key2 == k2)) {
				goto loopxxx;
			} else {
				push_key(key1,key2);
			}
		}
	}
	R_AX = $0200;
	INTR($16);
	Shift_Stat = ((R_AX & $0003) > 0);

	if ((k1 == 0) | ((k1 > 48) & (k1 < 58))) {
		if ((k1 > 48) & (k1 < 58)) {
			shift_stat = TRUE;
		}
		if (k2 == 75) {
			if (Shift_Stat && (mmode != 2)) {
				--x2;
				call clip_x2;
			} else if (x1 > min_x) {
				--x1;
				--x2;
			}
		} else if ((k2 == 77) & (x2 < max_x)) {
			if (Shift_Stat && (mmode != 2))  {
				++x2;
			} else {
				++x1;
				++x2;
			}
		} else if (k2 == 72) {
			if (Shift_Stat && (mmode != 2)) {
				--y2;
				call clip_y2;
			} else if (y1 > min_y) {
				--y1;
				--y2;
			}
		} else if (k2 == 80) {
			if (Shift_Stat && (mmode != 2)) {
				if (y2 < max_y) {
					++y2;
				}
			} else {
				if (!(uy2)) {
					if (y2 < max_y) {
						++y2;
						++y1;
					}
				} else {
					++y1;
					call clip_y1;
				}
			}
		} else if ((k1 == 0) && (k2 == 239)) {
			goto key_move_done;
		} else {
			goto key_loop2;
		}
		goto key_loop;
	} else if ((k1 == 27) | (k1 == 13)) {
		goto key_move_done;
	}
	goto key_loop2;

key_move_done:
		/* turn scrolllock off */
	poke( 0, $417, peek(0, $417) & $EF );
	check_key;
	goto move_done;

resize:
	Call Check_Width;
	Call Check_Height;
	if (mmode == 1) {
		size_window( x1, y1, x2, y2 );
		restore_box;
		redraw;
		call PUT_MESSAGE;
	} else if (mmode == 2) {
		restore_box_num( box_count );
		change_box_pos( box_count - 1, x1 - 1, y1 - 1 );
		restore_box_num( box_count - 1 );
		call PUT_MESSAGE;
	} else {
		restore_box;
		draw_outline(x1,y1,x2,y2,Error_Color);
		call PUT_MESSAGE;
		clear_virtual_display;
	}
	ret;

put_message:
	if (Use_Key) {
		Make_Message('|24|25|27|26 to move, Shift |24|25|27|26 to resize, <ESC> to exit.');
	}
	Update_Virtual_Window( 1, 1, screen_width, screen_length );
	Ret;

move_done:
	if (mmode < 2) {
		while (box_count > t_int) {
			kill_box;
		}
	}
	Mou_Set_Limits( 1, 1, Screen_Width, Screen_Length );
	Return_Str =
		'/X1=' + str(x1) +
		'/X2=' + str(x2) +
		'/Y1=' + str(Y1) +
		'/Y2=' + str(Y2);
	RETURN_INT = 1;
	if (mmode == 1) {
		w_bottom_line = o_bl;
		b_color = t_b_color;
		s_color = t_s_color;
		redraw;
		UPDATE_STATUS_LINE;
		update_virtual_display;
		clear_virtual_display;
		reset_virtual_display;
	} else if (mmode == 2) {
		kill_box;
		while (box_count > (bc - 1)) {
			pop_box;
		}
	} else {
		UPDATE_STATUS_LINE;
		update_virtual_display;
		reset_virtual_display;
	}
	goto exit;

check_width:
		if (ux1) {
			x1 = ox1;
		}
		if (ux2) {
			x2 = ox2;
		}
		w = x2 - x1;
		ret;

check_height:
		if (uy1) {
			y1 = oy1;
		}
		if (uy2) {
			y2 = oy2;
		}
		h = y2 - y1;
		ret;


clip_x1:
		if (x1 > (x2 - min_width)) {
			x1 = x2 - min_width;
		}
		if (x1 < min_x) {
			x1 = min_x;
		}
		ret;

clip_x2:
		if (x2 < (x1 + min_width)) {
			x2 = x1 + min_width;
		}
		if (x2 > max_x) {
			x2 = max_x;
		}
		ret;

clip_y1:
		if (y1 > (y2 - min_height)) {
			y1 = y2 - min_height;
		}
		if (y1 < min_y) {
			y1 = min_y;
		}
		ret;

clip_y2:
		if (y2 < (y1 + min_height)) {
			y2 = y1 + min_height;
		}
		if (y2 > max_y) {
			y2 = max_y;
		}
		ret;

draw_error_outline:
		bc = x2 - x1 + 1;
		draw_attr( x1, y1, error_color, bc );
		draw_attr( x1, y2, error_color, bc );
		bc = y2 - y1 + 1;
		jx = 0;
		while (jx < bc) {
			draw_attr( x1, y1 + jx, error_color, 1 );
			draw_attr( x2, y1 + jx, error_color, 1 );
			++jx;
		}
		ret;

exit:
		make_message("Window moved/resized");

/*  set_global_int('MENU_LEVEL', global_int('MENU_LEVEL') - 1); */
}

void ZOOM(int min = parse_int("/MINIMIZE=",mparm_str)) TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

NAME:  ZOOM

DESCRIPTION:  Replaces the ZOOM macro command.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  int  old_refresh, osn, jx, owin, t_id, /*min,*/ x, y;
	str tstr[80];

	old_refresh = refresh;
	refresh = false;
//  min = Parse_Int('/MINIMIZE=', mparm_str );
	tstr = global_str('!WINZOOM#' + str(window_id));

	osn = screen_num;
	if (min) {
		if ((window_attr & $40) == 0) {
			x = parse_int('/OMX=', tstr );
			y = parse_int('/OMY=', tstr );
			if ((x == 0) || (y == 0)) {
				x = win_x1;
				y = win_y1;
			}
			if (x == 0) {
				x = 1;
			}
			if (y == 0) {
				y = 1;
			}
			if (parse_str('/X1=', tstr) != '') {
				tstr = '/X1=' + parse_str('/X1=', tstr ) +
							'/Y1=' + parse_str('/Y1=', tstr ) +
							'/X2=' + parse_str('/X2=', tstr ) +
							'/Y2=' + parse_str('/Y2=', tstr );
			} else {
				tstr = '';
			}
			tstr = tstr +
							'/MX1=' + str(win_x1) +
							'/MX2=' + str(win_x2) +
							'/MY1=' + str(win_y1) +
							'/MY2=' + str(win_y2) +
							'/MSN=' + str(screen_num) + '/MZC=' + zoom_char +
							'/LO='  + str(left_offset);
			t_id = window_id;
			window_attr = $41;
//      RM('WINDOW^DelWin /NODEL=1');
      return_int = DELWIN(1,0,0);
			switch_win_id( t_id );
			window_attr = $40;
			size_window( x, y, x + length( truncate_path( file_name )) + 1, y + 1 );
			zoom_char = '|30';
			set_global_str('!WINZOOM#' + str(window_id), tstr + '/ZC=' + zoom_char );
      switch_win_id(global_int("!T_WIN!"));  // DELWIN returns the proper window to switch to
/*
			switch_window( cur_window + 1);
			owin = cur_window;
			rm('FINDWIN /NM=1');
			if (owin == cur_window) {
				rm('FINDWIN');
			}
 */
		}
	} else if ((window_attr & $40) != 0) {
		zoom_char = parse_str('/MZC=', tstr );
		window_attr = window_attr & $BF;
		screen_num = parse_int('/MSN=', tstr);
		x = win_x1;
		y = win_y1;
		left_offset = parse_int('/LO=', tstr);

/*
    size_window(Parse_Int('/MX1=', tstr),
								Parse_Int('/MY1=', tstr),
								Parse_Int('/MX2=', tstr),
								Parse_Int('/MY2=', tstr) );
*/
/* running this special mode of MOD_WIN instead of size_window() insures that
   split windows are taken into account */
    return_str = "/X1=" + Parse_str('/MX1=', tstr) +
                 "/Y1=" + Parse_str('/MY1=', tstr) +
                 "/X2=" + Parse_str('/MX2=', tstr) +
                 "/Y2=" + Parse_str('/MY2=', tstr);
//    RM("MOD_WIN /NOPROMPT=1");
    MOD_WIN(0,0,1);

		if (parse_str('/X1=', tstr) != '') {
			tstr = '/X1=' + parse_str('/X1=', tstr ) +
						'/Y1=' + parse_str('/Y1=', tstr ) +
						'/X2=' + parse_str('/X2=', tstr ) +
						'/Y2=' + parse_str('/Y2=', tstr );
		} else {
			tstr = '';
		}
		tstr = tstr + '/OMX=' + str(x) + '/OMY=' + str(y);
		set_global_str('!WINZOOM#' + str(window_id), tstr + '/ZC=' + zoom_char );
	} else if (parse_str('/X1=', tstr) != '') {
		screen_num = parse_int('/SN=', tstr);
		size_window( Parse_Int('/X1=', tstr),
								Parse_Int('/Y1=', tstr),
								Parse_Int('/X2=', tstr),
								Parse_Int('/Y2=', tstr) );
		tstr = '/OMX=' + str(parse_int('/OMX=', tstr )) +
						'/OMY=' + str(parse_int('/OMY=', tstr ));
		zoom_char = '|30';
		set_global_str('!WINZOOM#' + str(window_id), tstr + '/ZC=' + zoom_char);
	} else if ((win_x1 != min_window_col) | (win_y1 != min_window_row) |
			(win_x2 != max_window_col) | (win_y2 != max_window_row)) {
		if (parse_str('/OMX=', tstr) != '') {
			tstr = '/OMX=' + parse_str('/OMX=', tstr ) +
						'/OMY=' + parse_str('/OMY=', tstr );
		} else {
			tstr = '';
		}
		tstr = tstr + '/X1=' + str(win_x1) +
						'/X2=' + str(win_x2) +
						'/Y1=' + str(win_y1) +
						'/Y2=' + str(win_y2) +
						'/OMX=' + str(parse_int('/OMX=', tstr )) +
						'/OMY=' + str(parse_int('/OMY=', tstr )) +
						'/SN=' + str(screen_num);
		screen_num = global_int('@SCREEN_COUNT') + 1;
		if (screen_num <= 1) {
			screen_num = 2;
		}
		set_global_int('@SCREEN_COUNT', screen_num );
		window_attr = window_attr & $BF;
		size_window(min_window_col, min_window_row, max_window_col, max_window_row );
		zoom_char = '|18';
		set_global_str('!WINZOOM#' + str(window_id), tstr + '/ZC=' + zoom_char );
	} else {
		goto exit;
	}

	new_screen;
	goto exit;

exit:
	if (auto_arrange_icons)
//    rm( 'WIN_ICON_ARRANGE');
    WIN_ICON_ARRANGE(0);
	refresh = old_refresh;
  RETURN();
}


/****************** Multi-Edit INTEGER Macro Function ***********************

 NAME:         WinViewCount()

 DESCRIPTION:  Returns the total number of windows that are NOT defined with
							 the $80 attribute.

*****************************07-19-93 10:25am*******************************/
int WinViewCount()
{
	int r = refresh, tw = cur_window,
			count = 0;
	refresh = false;

	do
	{
		switch_window( cur_window + 1 );
		if(!(window_attr & $80))
			++count;
	} while ( cur_window != tw );

	refresh = r;
	return(count);
}

macro SPLITWIN TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SPLITWIN

Description: Splits a window horizontally or vertically.

Parameters:  Prompts the user if no parameters are passed.

						/DIR=str	Direction (str is NOT case sensitive)
									str = Up
									str = Down
									str = Left
									str = Right
						/LN#=n   Number of lines (or columns) in the new window.
									 0 means use half of the original window.
						/ID=     ID of window to split with, bypasses user in put of file
										 name to split with.
            /WL=1 Show window list.
							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
		int  Old_Refresh, x,y,j1, j2, ty1, ty2, newx1,newy1, newx2, newy2 ;
    int  oldx1, oldx2, oldy1, oldy2, old_win, cur_screen,tw1,tw2;

		int  win1_id, win2_id ;
		int  num_lines, nl1, nl2, event_count, jx ;
		str  direction[10], event_str[20] ;

		if (Window_Count >= 100) {
			Error_Level = 1001;
			Goto Exit;
		}

		direction = caps(parse_str('/DIR=', mparm_str));
		num_lines = parse_int('/LN#=', mparm_str);

		Old_Refresh = Refresh;
		Refresh = False;

		oldx1 = win_x1;
		oldx2 = win_x2;
		oldy1 = win_y1;
		oldy2 = win_y2;

		newx1 = oldx1;
		newx2 = oldx2;
		newy1 = oldy1;
		newy2 = oldy2;
		old_win = cur_window;

		if (num_lines == 0) {
			nl1 = (newy2 - newy1) / 2;
			nl2 = (newx2 - newx1) / 2;
		} else {
			nl1 = num_lines;
			nl2 = num_lines;
		}

		if (direction != '') {
			goto loopout;
		}
		x = win_x1 + (((win_x2 - win_x1) / 2) - 15);
		if (x < 1) {
			x = 1;
		}
		if ((x + 29) > screen_width) {
			x = screen_width - 29;
		}
		y = win_y1 + (((win_y2 - win_y1) / 2) - 6);
		if (y < min_window_row) {
			y = min_window_row;
		}
		if (y < 2) {
			y = 2;
		}
		if ((y + 11) > (max_window_row - 1)) {
			y = (max_window_row - 12);
		}
		event_count = 5;
		Set_Global_Str('@WSEV#1', '/3D=1/T=/KC=  |16  /K1=0/K2=77/R=1/W=7/X=' + str( x + 19) + '/Y=' + str(y + 4));
		Set_Global_Str('@WSEV#2', '/3D=1/T=/KC=  |17  /K1=0/K2=75/R=2/W=6/X=' + str( x +  3) + '/Y=' + str(y + 4));
		Set_Global_Str('@WSEV#3', '/3D=1/T=/KC=  |30  /K1=0/K2=72/R=3/W=4/X=' + str( x + 11) + '/Y=' + str(y + 2));
		Set_Global_Str('@WSEV#4', '/3D=1/T=/KC=  |31  /K1=0/K2=80/R=4/W=6/X=' + str( x + 11) + '/Y=' + str(y + 6));
		Set_Global_Str('@WSEV#5', '/3D=1/T=Cancel/KC=<ESC>/K1=27/K2=1/R=0/W=11/X=' + str( x + 8) + '/Y=' + str(y + 9));
		Put_Box(x,y, x + 29, y+12, 0, m_b_color, 'Indicate side to split', true);
		RM('USERIN^CheckEvents /M=2/G=@WSEV#/#=5');
loop:
		read_key;
	return_int = 0;
	if ((key1 == 0) & (key2 == 250)) {
		RM('CheckEvents /G=@WSEV#/#=5/M=1');
	} else {
		RM('CheckEvents /G=@WSEV#/#=5/M=0');
	}
	if (return_int != 0) {
		return_int = parse_int('/R=', return_str);
		set_global_int('s_direction',return_int);  /* MODIFIED 10-3-89 BY SUE FOR USE WITH FILE COMPARISON MACRO */
		if (return_int == 0) {
			kill_box;
			goto exit;
		} else if (return_int == 3) {
			direction = 'UP';
		} else if (return_int == 4) {
			direction = 'DOWN';
		} else if (return_int == 1) {
			direction = 'RIGHT';
		} else if (return_int == 2) {
			direction = 'LEFT';
		} else {
			goto loop;
		}
		goto loopout;
	}
	goto loop;

loopout:
		kill_box;

			if (direction == 'UP') {
				newy2 = newy1 + nl1;
				oldy1 = newy2 + split_mode;
			} else if (direction == 'DOWN') {
				newy1 = newy2 - nl1;
				oldy2 = newy1 - split_mode;
			} else if (direction == 'RIGHT') {
				newx1 = newx2 - nl2;
				oldx2 = newx1 - split_mode;
			} else if (direction == 'LEFT') {
				newx2 = newx1 + nl2;
				oldx1 = newx2 + split_mode;
			} else {
				goto exit;
			}

		Size_Window(oldx1,oldy1,oldx2,oldy2);
	/*   IF (direction = 'DOWN') THEN
			w_bottom_line := false;
		END;
	 */
		if (screen_num == 0) {
			screen_num = global_int('@SCREEN_COUNT') + 1;
			if (screen_num <= 1) {
				screen_num = 2;
			}
			set_global_int('@SCREEN_COUNT', screen_num );
		}
		cur_screen = screen_num;
		refresh = true;
		redraw;
		refresh = false;


		Win2_Id = Parse_Int('/ID=', MParm_Str);

		if (Win2_Id) {
			Win1_Id = Window_Id;
			Switch_Win_Id(Win2_Id);
			Size_Window(newx1,newy1,newx2,newy2);
			Screen_Num = Cur_Screen;
			Switch_Win_Id(Win1_id);
			Goto Exit;
		}

		switch_window( window_count );
		Create_Window;
		RM('EXTSETUP /PRE=1');
		Size_Window(newx1,newy1,newx2,newy2);

		screen_num = cur_screen;

	 /* RM('setbtms /SN=' + str(cur_screen)); */
		j2 = cur_window;

		Refresh = true;
		Redraw;
		tw1 = window_id;
		return_int = 0;
		if (parse_int('/WL=', mparm_str)) {
			if( WinViewCount() > 2 )
			{
				RM( 'WINMENU /WT=LINK WINDOW/WH=Select window to link to and press <ENTER>, or <ESC> to abort/HS=WINLINK/HE=0/DB=0/DC=0' + MParm_Str);
				if (Return_Int != 0) {
					Link_Window(Return_Int);
					Return_Int = 1;
					RM('SetWindowNames');
					Return_Int = 1;
				}
			}
		}
		if (return_int == 0) {
			RM('MEUTIL1^LOADFILE /X=' + str(win_x1) + '/Y=' + str(win_y1));
			if (return_int == 0) {
				link_window(old_win);
				make_message( 'Window linked.');
			} else {
				tw2 = window_id;
				j1 = cur_window;
				if (tw2 != tw1) {
					refresh = false;
					if (switch_win_id( tw1 )) {
						if (caps(file_name) == '?NO-FILE?') {
							link_window( j1 );
							make_message( 'Window linked.');
						}
					}
				}
			}
		}
		refresh = false;
		RM('SetWindowNames');
//    if (auto_arrange_icons)
//      rm( 'WIN_ICON_ARRANGE ' );
Exit:
		RM('CheckEvents /G=@WSEV#/#=5/M=3');
}

macro WOrganize TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:				WOrganize

Description:	Organizes all non hidden windows on the screen by either
							cascading, tiling or minimizing them.

Parameters:   /M=0		Cascade
							/M=1		Tile
							/M=2		Minimize
							/BTM=1  Puts icons on the bottom of the screen.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  x1, y1, x2, y2,xscreen_num,
					m = parse_int('/M=', mparm_str)
					;
	int  original_window = cur_window,
					 jx, total_displayed_win = 0,
					 w_width, w_length, w_rows, column_count,
					 v_overlap = 0, h_overlap = 0,
					 off_rows,
						mask = $81
					;

	str  tstr[80] ;

	working;
	refresh = FALSE;
	xscreen_num = global_int('@SCREEN_COUNT');
	++xscreen_num;
	if (xscreen_num <= 1) {
		xscreen_num = 2;
	}
	tstr = global_str('@INIT_WINDOW_PARMS');
	x1 = parse_int('/X1=', tstr);
	x2 = parse_int('/X2=', tstr);
	y1 = parse_int('/Y1=', tstr);
	y2 = parse_int('/Y2=', tstr);
	if (x1 < min_window_col) {
		x1 = min_window_col;
	}
	if ((x2 == 0) || (x2 > max_window_col)) {
		x2 = max_window_col;
	}
	if (y1 < min_window_row) {
		y1 = min_window_row;
	}
	if ((y2 == 0) || (y2 > max_window_row)) {
		y2 = max_window_row;
	}
	set_virtual_display;
	jx = min_window_row;
	while (jx <= max_window_row) {
		Draw_Char(177, 1, jx, background_color, screen_width );
		++jx;
	}

	if (m == 1) {
		call calc_for_tiles;
	} else if (m == 2) {
		tstr =
					'/MX1=' + str(x1) +
					'/MX2=' + str(x2) +
					'/MY1=' + str(y1) +
					'/MY2=' + str(y2);

//		call calc_for_icons;
		mask = $81;
	}

	refresh = FALSE;
	jx = 0;
	while (jx < window_count) {
		++jx;
		if ((jx != original_window) | (m == 2)) {
			switch_window( jx );
			if ((window_attr & mask) == 0) {
				call do_size;
			}
		}
	}

	if (m != 2) {
		switch_window( original_window );
		call do_size;
	}
	else
//    rm("win_icon_arrange");
    WIN_ICON_ARRANGE(0);

	jx = 1;
	switch_window( 1 );
	refresh = TRUE;
	redraw;
	while (jx < window_count) {
		++jx;
		if (jx != original_window) {
			switch_window( jx );
		}
	}
	goto exit;

do_size:
	if (m == 2) {
		call minimize;
		ret;
	} else {
		set_global_str('!WINZOOM#' + str(window_id), '');
		window_attr = window_attr & $BF;
	}
	screen_num = xscreen_num;
	size_window( x1, y1, x2, y2 );
	++xscreen_num;
	if (xscreen_num <= 1) {
		xscreen_num = 2;
	}
	if (m == 1) {
		--total_displayed_win;
		if (total_displayed_win > 0) {
			x1 = x1 + w_width - h_overlap;
			if (x1 > screen_width) {
				x1 = 1;
				if (total_displayed_win < column_count) {
					column_count = total_displayed_win;
					w_width = (screen_width / column_count) + ((screen_width % w_width) > 0);
				}
				y1 = y1 + w_rows;
			}
			x2 = x1 + w_width - 1;
			if (x2 > screen_width) {
				x2 = screen_width;
			}
			y2 = y1 + w_length;
			if (y2 > max_window_row) {
				y2 = max_window_row;
			}
		}
	} else if (m == 2) {
		x1 = x1 + 16;
		if (x1 > screen_width) {
			x1 = 1;
			++y1;
		}
		x2 = x1 + 15;
		y2 = y1;
	} else {
		if ((x2 - x1) > 20) {
			++x1;
		}
		if ((y2 - y1) > 3) {
			++y1;
		}
	}
	RET;

minimize:
	set_global_str('!WINZOOM#' + str(window_id), '/ZC=' + zoom_char +
					 tstr +	'/MSN=' + str(screen_num) + '/MZC=|30');
	window_attr = $40;
	zoom_char = '|30';
	ret;

calc_for_tiles:
	x1 = 1;
	x2 = screen_width;
	y1 = min_window_Row;
	y2 = max_window_Row;
	jx = 0;
	while (jx < window_count) {
		++jx;
		switch_window( jx );
		if ((window_attr & mask) == 0) {
			++total_displayed_win;
		}
	}
	column_count = (total_displayed_win / 4) + ((total_displayed_win % 4) > 0);
	if (column_count < 2) {
		column_count = 2;
	}
	w_width = screen_width / column_count;
	if (w_width < 20) {
		column_count = screen_width / 20;
		w_width = 20;
	}
	w_width = w_width + ((screen_width % w_width) > 0);
	jx = (total_displayed_win / column_count) + ((total_displayed_win % column_count) != 0);
	if (jx == 0) {
		jx = 1;
	}
	w_rows = (y2 - y1 + 1) / jx;
	off_rows = (y2 - y1 + 1) % jx;
	if (w_rows < 1) {
		w_rows = 1;
	}
	w_length = w_rows - 1;
	if (w_length < 2) {
		w_length = 2;
	}
	x1 = 1;
	x2 = w_width;
	y2 = y1 + w_length;
	ret;

/*
calc_for_icons:
	x1 = 1;
	if (parse_int('/BTM=', mparm_str)) {
		jx = 0;
		while (jx < window_count) {
			++jx;
			switch_window( jx );
			if ((window_attr & $81) == 0) {
				++total_displayed_win;
			}
		}
		column_count = screen_width / 16;
		jx = (total_displayed_win / column_count) +
					((total_displayed_win % column_count) > 0);
		y1 = max_window_row - jx  + ((max_window_row != fkey_row) & (max_window_row != status_row));
		if (y1 < min_window_row) {
			y1 = min_window_row;
		}
	}
	y2 = y1;
	x2 = 15;
	ret;
*/

exit:
	set_global_int('@SCREEN_COUNT', xscreen_num );
	refresh = TRUE;
	switch_window( original_window );
	update_status_line;
	redraw;
	update_virtual_display;
	reset_virtual_display;
}


macro WINOP TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: WINOP

Description:  Performs various window operations

Parameters:		/T=0	Create window
							/T=1  Delete window
							/T=2  Hide window
							/T=3  Window list
							/T=4  Split window
							/T=5  Resize window
							/T=6  Link window
							/T=7  Unlink window
							/T=8  Zoom window
							/T=9  Delete all windows

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  tt ;

	switch (Parse_Int('/T=', mparm_str)) {
		case 0 :
			RM('MAKEWIN');
			break;
		case 1 :
			RM('DELWIN ' + mparm_str);
			make_message("");
			break;
		case 2 :
			Window_Attr = Window_Attr | $01;
			RM( 'FindWin /NM=1' );
			New_Screen;
			break;
		case 3 :
			RM( 'SwitWin' );
			break;
		case 4 :
			RM( 'SPLITWIN /WL=1' );
			break;
		case 5 :
//      RM('MOD_WIN');
      MOD_WIN(0,0,0);
			break;
		case 6 :
			RM('LINKWIN ' + mparm_str);
			break;
		case 7 :
			unlink_window;
			break;
		case 8 :
			RM('zoom');
			break;
		case 9 :
			do {
				tt = window_count;
//        rm("DELWIN");
        DELWIN(0,0,0);
			}	while (tt > window_count);
			make_message("");
			break;
	}
}

macro WIN_MENU_SET trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: WIN_MENU_SET

Description:  Sets up menu disable globals for the window menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	RM('SCREEN_SHARE /M=1');
	set_global_int('!WIN_DIS_1',(window_attr & $40) != 0);
	set_global_int('!WIN_DIS_2',(return_int & $80) != 0);
	set_global_int('!WIN_DIS_3',(return_int & $7F) == 0);
	set_global_int('!WIN_DIS_4',link_stat == 0);
}
