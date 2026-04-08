macro_file USERIN;
/*******************************************************************************
														MULTI_EDIT MACRO FILE

Name: USERIN

Description:  Most of the general purpose user input routines.

TOPMENU   - Macro to create a top level menu.
SUBMENU   - Creates a sub level menu.
XMENU     - Replaces the V_Menu and Bar_Menu macro commands.
QUERYBOX  - A general purpose boxed string input prompt.
VERIFY    - Are you sure?
CHECKFILE - A verify for files.
MAINHELP  - Accesses the main ME help screen.


		Found in USERIN1.S:

DATA_IN         - A general purpose Boxed Data entry menu.
DI_STRING_IN    - String input field handler for DATA_IN only.
DI_LIST_FIELD   - List field input handler for DATA_IN only.
DI_FKEY_ROW     - Funtion key row help handler for DATA_IN only.
DI_DIR_FIELD    - Directory field handler for DATA_IN only.
DI_EDIT_WINDOW  - Edit window field handler for DATA_IN only.
HISTORY_LIST    - History list handler for DATA_IN only.


		Found in USERIN2.S:

SPECCHAR        - Changes untypeable character into the |xx convention.
VALCHAR         - Changes the |xx back to a character.
STRSRC          - Formats strings to be used in macro source code generators.
CHNGPARM        - Changes a single slash type parameter(/) in a global string.
GLOBALVARLIST		- Creates a menu of global pseudo-array elements.
DELETEITEM      - Shuffles global variable arrays.
CHECKEVENTS     - Mouse and Key event handler.
DB              - General purpose Database manager.
DB_MOVE_ITEM		- Moves records in a DB file.  For use by DB only!
DB_SEARCH				- Performs a search on a DB file.  For use by DB only!
EDITWINDOW      - File editing/examining macro.
EDITSPELL				- Invokes the spell checker while in EDITWINDOW.
EDITSEARCH			- Invokes search while in EDITWINDOW.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

macro TOPMENU trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO
Name: TOPMENU

Description:  Creates the top bar menu.

Parameters:   /HN=1     Use the handle in RETURN_INT, ignore the globals
							/#=nn     The number of menu selections.
							/S=nn     The starting menu selection number.
							/G=str    The prefix used to find the global strings containing
													the individual menu selection parameters.
							/M=str    The prefix used to find the actual menu name strings.
													If this parameter is used then TOPMENU assumes that
													the menu item names will be contained in seperate
													globals instead of being part of the selection
													parameters.
							/X=nn     The starting column.
							/Y=nn     The starting row.
							/L=str    The label for the menu
							/B=nn     0 = Create a box for the menu
												1 = Don't create the box.
							/GCLR=1   Clear all globals on exit;

							The individual menu items are passed via global strings defined
							as the string passed via /G= plus the number of the menu item.
							If "/G=MSTR" then menu item one would be "MSTR1", item two would
							be "MSTR2" and so on.

							Each menu item parametr string may contain the following:

							/N=str    The name of the menu item.  Use only if /M (above)
													is NOT used.
							/S=nn     0 = The item has a sub-menu.

												1 = The item does not have a sub-menu, but do not
														delete this menu and return to this menu with
														the following action according to Return_Int
															Return_Int = 0 - Return to this menu and process the
																last keystroke.  In this case, right and left
																arrow keys will cause the choice to the left or
																right of the current choice to be selected, and
																<ESC> will cause an exit from this menu.
															Return_Int = -1 - Return to this menu only.  Do not
																Process the last keystroke.
															Return_Int > 1 - Immediately after returning to
																this menu, exit this menu.

												2 = The item does not have a sub-menu.  Delete this
														menu from the screen, execute the macro, do
														not return to this menu.
							/H=str    Help index string for this menu item.
							/M=str    Macro to run upon selection of this menu item.
												This must be the last parameter in the menu string
												because everything after the /M= is passed to the
												macro as its parameters.  /X=, /Y= and /BC= are also
												passed to the macro.  /BC= is the box number above
												which all boxes are to be removed.


							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str
					Mstr,
					Label_Str[80]
					;

	str  GStr[20], gstr2[20] ;

	int x1,y1,jx,start,bc, t_ex, t_box_count, res, first_time, old_x, old_y ;
	int  menu_type, sub_col ;
	int  start_box_count,
						select_stat,
						select_mode, Count, cur_item,
					menu_handle = RETURN_INT,
					T_Refresh = Refresh ;

	old_x = wherex;
	old_y = wherey;

	Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') + 1);
	if(  parse_int('/HN=', mparm_str)  ) {
		menu_type = 3;
	} else {
		menu_handle = menu_create;
		menu_type = 1;
		gstr2 = Parse_Str('/M=', mparm_str);
		if(  gstr2 == ''  ) {
			gstr2 = '@#$X' + str( global_int('MENU_LEVEL') );
			menu_type = 0;
		}
	}
	refresh = false;
	gstr = Parse_Str('/G=', mparm_str );
	x1 = Parse_Int('/X=',MPARM_STR);
	y1 = Parse_Int('/Y=',MPARM_STR);
	if(  y1 == 0  ) {
		y1 = min_window_row + 1;
	}
	if(  (y1 + 4) > (screen_length)  ) {
		y1 = (screen_length - 4);
	}

	if(  y1 <= 0  ) {
		y1 = 2;
	}
	bc = (Parse_Int('/B=',MPARM_STR) == 0);
	count = Parse_Int('/#=', MParm_Str );
	Start = Parse_Int('/S=',MPARM_STR);
	if(  start < 1  ) {
		start = 1;
	}

	start_box_count = box_count;

	Select_Stat = 0;

	if(  menu_type == 0  ) {
		JX = 0;
		while(  jx < Count  ) {
			++jx;
			mstr = Global_Str(gstr + str(jx));
			label_str = parse_str( '/N=', mstr );
			res = 0;
			if(  str_char(label_str, svl(label_str)) == '|254'  ) {
				res = 1;
				label_str = copy(label_str, 1, svl(label_str) - 1 );
			}
			menu_set_item( menu_handle, jx, label_str, '', mstr,  res, 0, 0);
		}
	} else if(  menu_type == 1  ) {
		JX = 0;
		while(  jx < Count  ) {
			++jx;
			label_str = global_str( gstr2 + str(jx) );
			res = 0;
			if(  svl(label_str) > 0  ) {
				if(  str_char(label_str, svl(label_str)) == '|254'  ) {
					res = 1;
					label_str = copy(label_str, 1, svl(label_str) - 1 );
				}
			}
			menu_set_item( menu_handle, jx, label_str, '', Global_Str(gstr + str(jx)), res, 0, 0 );
		}
	}
	label_str = parse_str('/L=', mparm_str);

	t_box_count = box_count;

	cur_item = Start;

	cur_item = 0;
/*  while cur_item < count do
		++cur_item;
		call draw_item;
	end;
 */
	cur_item = start;
	first_time = 0;
	update_status_line;
main_loop:
	jx = x1 + first_time;
	menu_go(menu_handle, count,jx, y1 + first_time, (bc << 4) | (first_time << 8), bc - first_time , label_str, cur_item, res, sub_col );
	sub_col -= 2;
	if (sub_col < 1)
			sub_col = 1;

	x1 = jx - first_time;
	set_global_str( gstr + '0', str(cur_item) );
	t_box_count = box_count;
	first_time = bc;
	call draw_item;
	select_mode = Parse_int('/S=',mstr);

	if(  res == -4  ) {
		if(  (Mou_Last_Y == Fkey_Row)  ) {
			RM( 'MOUSE^MouseFkey' );
		} else {
			push_key(key1,key2);
			return_int = 0;
			goto exit;
		}
	} else if(  res == -2  ) {
		if(  key1 == 0  ) {
			if(  key2 == 59  ) {
				Jx = Cur_Item;
SEEK_HELP:
				if(  (Jx > 1) & (parse_str('/H=',menu_item_str( menu_handle,Jx,3)) == '')  ) {
					--Jx;
					Goto SEEK_HELP;
				}
				help( parse_str('/H=', menu_item_str( menu_handle,Jx,3)));
			}
		}
	} else if(  res == 0  ) {
		return_int = 0;
		goto exit;
	}
	if(  res > 0   ) {
		select_stat = true;
		if(  select_mode && (select_mode != 5) ) {
			goto do_select;
		}
	}

	if(  select_stat && (!select_mode || (select_mode == 5))  ) {
do_select:
		if(  (select_mode == 2)  ) {
			hmenu_stat = FALSE;
			if(  bc  ) {
				kill_box;
			}
		}
		mstr = menu_item_str( menu_handle,cur_item,3);
		jx = Xpos('/M=', mstr, 1);
		if(  jx == 0  ) {
			goto nomstr;
		}
		mstr = copy(mstr,jx + 3, 200);
		if(  mstr != ''  ) {
			RM( mstr +
				' /BC=' + str(start_box_count) + '/X=' + str(sub_col) + '/Y=' + str(y1 + 2 - (bc == 0)));
			update_status_line;
		} else {
	nomstr:
			return_int = cur_item;
		}
		hmenu_stat = FALSE;
		if(  select_mode == 2  ) {
			goto exit;
		}

		while(  box_count > t_box_count  ) {
			kill_box;
		}
		if(  return_int == -2  ) {
			select_stat = 0;
		} else if(  return_int == -1  ) {
			select_stat = 0;
			goto exit;
		} else if(  return_int == 0  ) {
			select_stat = 1;
			push_key( key1, key2 );
		} else if(  return_int > 0  ) {
			goto exit;
		}
	}

	goto main_loop;

draw_item:
	mstr =  menu_item_str( menu_handle,cur_item,3);
	ret;

exit:
	hmenu_stat = FALSE;
	while(  box_count > t_box_count  ) {
		kill_box;
	}

	if(  bc  ) {
		if(  box_count == t_box_count  ) {
			kill_box;
		}
	}

	if(  menu_type < 3  ) {
		menu_delete( menu_handle );
		jx = 0;
		while(  jx < count  ) {
			++jx;
			set_global_str(gstr + str(jx), '');
			set_global_str(gstr2 + str(jx), '');
		}
	}
	Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') - 1);
	if(  mode != edit  ) {
		gotoxy( old_x, old_y);
	}
exitx:
	update_virtual_display;
	Refresh = T_Refresh;
}


macro SUBMENU trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SUBMENU

Description:  Creates a boxed vertical menu.  Used by DATA_IN.

Returns:    Return_Int = -1 if <ESC> was pressed.
													0 if the <LEFT> or <RIGHT> keys were pressed.
												 >0 if an item was selected.

Parameters:   /HN=1     RETURN_INT contains the handle to the menu, ignore
													global strings.
							/#=nn     The number of menu selections.
							/S=nn     The starting menu selection number.
							/G=str    The prefix used to find the global strings containing
													the individual menu selections.
							/M=str    The prefix used to find the actual menu name strings.
													If this parameter is used then SUBMENU assumes that
													the menu item names will be contained in seperate
													globals instead of being part of the selection
													parameters.
							/X=nn     The starting column.
							/Y=nn     The starting row.
							/L=str    The label for the menu
							/A=nn     0 = Exit if the left or right arrow keys are pressed.
												1 = Ignore left and right arrow keys.
							/B=nn     0 = Create a box for the menu
												1 = Don't create the box.
							/BC=nn    The Box number above which all boxes will be removed
												if a menu item with /S=2 (as a parameter) is selected.
												0 if all boxes are to be removed.
							/GCLR=1   Clear all globals on exit;
							/BO=      Box offset.  Normally used with /B=1.  Will offset
												The menu as though a box was there.  Good for multiple
												calls without kill the box in between.

							The individual menu items are passed via global strings defined
							as the string passed via /G= plus the number of the menu item.
							If "/G=MSTR" then menu item one would be "MSTR1", item two would
							be "MSTR2" and so on.

							Each menu item may contain the following:

							/N=str    The name of the menu item.  Use only if /M (above)
													is NOT used.
							/S=nn     0 = The item has a sub-menu.
												1 = The item does not have a sub-menu, but do not
														delete this menu and return to this menu
														selection if the macro returns 0.
												2 = The item does not have a sub-menu.  Delete this
														menu from the screen, execute the macro, do
														not return to this menu.
												3 = Run the sub-menu.  Exit this menu with return_int
														equal to the menu selection.  Don't kill box.
												4 = Same as 2, but RETURN_INT returns whatever the
														macro that got run returns, NOT the menu item #
												5 = Same as 0, but RETURN_INT returns whatever the
														macro that got run returns, NOT the menu item #
							/H=str    Help index string for this menu item.
							/M=str    Macro to run upon selection of this menu item.
												This must be the last parameter in the menu string
												because everything after the /M= is passed to the
												macro as its parameters.  /X=, /Y= and /BC= are also
												passed to the macro.



							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str  Mstr, Label_Str[80] ;
	str  gstr[20], gstr2[20] ;
	int  first_time, res, sub_col ;

	int  bc, t_box_count, kill_count, arrow_stat ;
	int  x1,y1,jx,start, select_mode;
	int  Count, cur_item, menu_type, bo, old_x, old_y,
					T_Refresh = Refresh,
					menu_handle = RETURN_INT
						;

	old_x = wherex;
	old_y = wherey;
	Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') + 1);

	if(  parse_int('/HN=', mparm_str)  ) {
		menu_type = 3;
	} else {
		menu_handle = menu_create;
		menu_type = 1;
		gstr2 = Parse_Str('/M=', mparm_str);
		if(  gstr2 == ''  ) {
			menu_type = 0;
		}
	}
	gstr = Parse_Str('/G=', mparm_str );
	x1 = Parse_Int('/X=',MPARM_STR);
	y1 = Parse_Int('/Y=',MPARM_STR);
	count = Parse_Int('/#=', MParm_Str );
	Start = Parse_Int('/S=',MPARM_STR);

	bo = parse_int('/BO=', mparm_str);

	if(  start < 1  ) {
		start = 1;
	}
	kill_count = parse_int('/BC=', mparm_str );
	bc = (Parse_Int('/B=', mparm_str) == 0);
	arrow_stat = Parse_Int('/A=', mparm_str);

	if(  menu_type == 0  ) {
		JX = 0;
		while(  jx < Count  ) {
			++jx;
			mstr = Global_Str(gstr + str(jx));
			label_str = parse_str( '/N=', mstr );
			res = 0;
			if(  svl(label_str) > 0  ) {
				if(  str_char(label_str, svl(label_str)) == '|254'  ) {
					res = 1;
					label_str = copy(label_str, 1, svl(label_str) - 1 );
				}
			}
			menu_set_item( menu_handle, jx, label_str, '', mstr,  res, 0, 0);
		}
	} else if(  menu_type == 1  ) {
		JX = 0;
		while(  jx < Count  ) {
			++jx;
			label_str = global_str( gstr2 + str(jx) );
			res = 0;
			if(  svl(label_str) > 0  ) {
				if(  str_char(label_str, svl(label_str)) == '|254'  ) {
					res = 1;
					label_str = copy(label_str, 1, svl(label_str) - 1 );
				}
			}
			menu_set_item( menu_handle, jx, label_str, '', Global_Str(gstr + str(jx)), res, 0, 0 );
		}
	}

	label_str = Parse_Str('/L=', mparm_str );
	if(  y1 == 0  ) {
		y1 = min_window_row + 1;
	}

	if(  (y1 + count + (2 * bc)) > (screen_length)  ) {
		y1 = (screen_length - count - (2 * bc));
	}

	if(  y1 <= 0  ) {
		y1 = min_window_row;
	}


	cur_item = start;
	first_time = 0;
main_loop:
	update_status_line;
	jx = x1 + first_time + bo;
	menu_go(menu_handle, count,jx, y1 + first_time + bo,((bc & $01) << 4) + 1 , bc - first_time , label_str, cur_item, res, sub_col );
	x1 = jx - first_time - bo;
	set_global_str( gstr + '0', str(cur_item) );
	t_box_count = box_count;
	first_time = bc;
	select_mode = Parse_Int('/S=', menu_item_str( menu_handle, cur_item, 3));
	switch( res )
	{
		case -10 :
			rm('MEHELP^ScreenMrk');
			break;
		case -4 :
			if(  (Mou_Last_Y == Fkey_Row)  ) {
				RM( 'MOUSE^MouseFkey' );
			} else {
				push_key(key1,key2);
				if(  bc  ) {
					kill_box;
				}
				return_int = -2;
				goto exit;
			}
			break;
		case -2 :
			if(  key1 == 0  ) {
				if(  key2 == 59  ) {
					Jx = Cur_Item;
					mstr = parse_str('/H=', menu_item_str( menu_handle, Jx, 3));
					if(  mstr == ''  ) {
						mstr = parse_str('/H=', mparm_str);
						if(  mstr == ''  ) {
	SEEK_HELP:
							if(  ((Jx > 1) & (parse_str('/H=',menu_item_str( menu_handle, Jx,3)) == ''))  ) {
								--Jx;
								Goto SEEK_HELP;
							}
							mstr = parse_str('/H=', menu_item_str( menu_handle, Jx,3) );
						}
					}
					help( mstr );
					goto main_loop;
				}
			}
			if(  arrow_stat == 0  ) {
				if(  bc  ) {
					kill_box;
				}
				return_int = 0;
				goto exit;
			}
			break;

		case 0:
			return_int = -1;
			if(  bc  ) {
				kill_box;
			}
			goto exit;
		default:
			if(  res > 0  ) {
				goto do_select;
			}
	}
	goto main_loop;

do_select:
		if(  (select_mode == 2) || (select_mode == 4) ) {
			while(  box_count > kill_count  ) {
				kill_box;
			}
		}
		mstr = menu_item_str( menu_handle, cur_item, 3);
		jx = Xpos('/M=', mstr, 1);
		if(( jx == 0 ) || parse_int('/NM=', mstr)) {
			return_int = cur_item;
			goto exit;
		}
		mstr = copy(mstr,jx + 3, 200);
		if( (select_mode != 2) && (select_mode != 4) ) {
			mstr = mstr + ' /BC=' + str(kill_count) +
				'/X=' + str(x1 + 1) + '/Y=' + str(y1 + 1 + cur_item);
		}
		RM( mstr );
		if(  (select_mode == 2) || (select_mode == 3)  ) {
			return_int = cur_item;
			goto exit;
		}
		else if( select_mode == 4 )
			goto exit;

		if(  return_int > 0  ) {
			while(  box_count > t_box_count  ) {
				kill_box;
			}
			if(  bc  ) {
				if(  box_count == t_box_count  ) {
					kill_box;
				}
			}
			if(  not(select_mode)  ) {
				while(  box_count > kill_count  ) {
					kill_box;
				}
			}
			if( select_mode !=5 )
				return_int = res;
			goto exit;
		}

		goto main_loop;

exit:
	if(  menu_type < 3  ) {
		menu_delete( menu_handle );
		if(  (parse_int('/GCLR=', mparm_str) == true) | (menu_type == 0)  ) {
			jx = 0;
			while(  jx < count  ) {
				++jx;
				set_global_str(gstr + str(jx), '');
				set_global_str(gstr2 + str(jx), '');
			}
		}
	}
	Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') - 1);
	if(  mode != edit  ) {
		gotoxy( old_x, old_y);
	} else
		gotoxy( 0, 0 );
	Refresh = T_Refresh;

}


macro XMENU trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: XMENU

Description: Generates a vertical or horizontal menu.  Meant to be a replacement
							for the macro functions V_MENU and BAR_MENU.

Returns:      Return_Int = 0 if <ESC> was pressed
												 > 0 then return_int is the number of the select
														 menu item.

Parameters:  /X=nn  Starting column coordinate
						 /Y=nn  Starting row coordinate
						 /B=nn  0 = No box
										1 = Create box.
						 /T=nn  0 = Horizontal menu
										1 = Vertical menu
						 /S=nn  Start menu item.
						 /L=str Label for box
						 /M=str The menu string.

										The format is as follows:

										Off(INDENT)Auto()Smart()

										Menu titles are seperated by ()'s
										Inside the ()'s are the help indexes
										If the help index is the same for all menu
										options you can just specify the first.

										Must be the LAST parameter passed.

						 /M1= - M3= The names of global strings to be added to /M= in case
										you need more menu choices than will fit on the 254
										character macro command line.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str  bstr[4] ;
	int  start,x1,y1,box,jx, jx2, jy, count, menu = menu_create, res ;
	str  mstr, itemname[80], helpstr[40] ;

	x1 = parse_int( '/X=', mparm_str );
	y1 = parse_int( '/Y=', mparm_str );
	box = parse_int( '/B=', mparm_str );
	if(  box  ) {
		bstr = '';
	} else {
		bstr = '/B=1';
	}
	start = parse_int( '/S=', mparm_str );

	set_global_int('MENU_LEVEL', global_int('MENU_LEVEL') + 1);

	jx = xpos( '/M=', mparm_str , 1);
	mstr = copy( mparm_str, jx + 3, 254 );
	count = 0;

	call get_menus;
	mstr = global_str( parse_str( '/M1=', mparm_str ) );
	call get_menus;
	mstr = copy(mstr, jx2, 254) + global_str( parse_str( '/M2=', mparm_str ) );
	call get_menus;
	mstr = copy(mstr, jx2, 254) + global_str( parse_str( '/M3=', mparm_str ) );
	call get_menus;
	goto do_menu;

get_menus:
	jx = 1;
	jx2 = 1;
loop:
	jx = xpos( '(', mstr, jx + 1 );
	if(  jx != 0  ) {
		if(  copy(mstr, jx + 1, 1) == '('  ) {
			mstr = str_del( mstr, jx, 1);
			goto loop;
		}
		++count;
		jy = xpos( ')', mstr, jx + 1);
		helpstr = copy( mstr, jx + 1, jy - jx - 1 );
		itemname = copy( mstr, jx2, jx - jx2 );
		res = 0;
		if(  svl(itemname) > 0  ) {
			if(  str_char( itemname, svl(itemname) ) == '|254'  ) {
				itemname = copy(itemname, 1, svl(itemname) - 1 );
				res = 1;
			}
		}
		menu_set_item( menu, count, itemname, '', '/S=2/H=' + helpstr, res, 0, 0 );
		jx2 = jy + 1;
		goto loop;
	}
	ret;

do_menu:
	return_int = menu;
	if(  parse_int('/T=',mparm_str) == 0  ) {
		RM('TOPMENU /HN=1/X=' + str(x1) +
											'/Y=' + str(y1) +
											'/#=' + str(count) +
											'/S=' + str(start) +
											'/G=!XMENU' + bstr +
											'/BC=' + str(box_count) +
											'/L=' + parse_str('/L=', mparm_str)
						);
	} else {
		RM('SUBMENU /HN=1/A=1/X=' + str(x1) +
											'/Y=' + str(y1) +
											'/#=' + str(count) +
											'/S=' + str(start) +
											'/G=!XMENU' + bstr +
											'/BC=' + str(box_count) +
											'/L=' + parse_str('/L=', mparm_str)
						);
	}
	if(  (Return_Int < 0)  ) {
		Return_Int = 0;
	}
	set_global_int('MENU_LEVEL', global_int('MENU_LEVEL') - 1);
	menu_delete( menu );
}


macro QUERYBOX TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:   QUERYBOX

Description:  Creates a simple text input dialog box.

Parameters:   Return_Str is initialized the default input string value.
							The first 2 parameters are not normally neccesary because the box
							will automatically position itself.
							/C=n    The column position
							/L=n    The line number
							/W=n    The maximum width of the string in the box
							/ML=n   The maximum length of the string.
							/T=str  The box title
							/H=str  The help index
							/N=1    Numeric input.  If Numeric input then Return_Int
												should be initialized to the default value.
							/P=str  Prompt.
							/MIN=n  For numeric only.  n = minimum legal response value.
							/MAX=n  For numeric only.  n = maximum legal response value.
							/HISTORY= The history global name.

Returns:      If NOT Numeric input then
								Return_Int = 1 if <ENTER> was pressed to accept the input.
								Return_Int = 0 if <ESC> was pressed.
								Return_Str = the inputted string.  Unchanged if Return_Int = 0.
							ELSE
								Return_Str = 'TRUE' if <ENTER> was pressed.
								Return_Str = 'FALSE' if <ESC> was pressed.
								Return_Int = The numeric result.  Unchanged if Return_Str = false.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int
					texp, x, y, jx,
					numeric, Tint,
					tbc,
					old_refresh,
					box,
					menu_level
					;

	str  Temp_Str[40] = ''
					;

	Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') + 1);
	menu_level = global_int('MENU_LEVEL');
	old_refresh = refresh;
	texp = explosions;
	explosions = false;
	refresh = false;
	x = Parse_Int('/C=',MParm_Str);
	y = Parse_Int('/L=',MParm_Str);
	numeric = Parse_Int('/N=', MParm_Str);
	if(  xpos('/MIN=', mparm_str, 1)  ) {
		temp_str = temp_str + '/MIN=' + parse_str('/MIN=', mparm_str);
	}
	if(  xpos('/MAX=', mparm_str, 1)  ) {
		temp_str = temp_str + '/MAX=' + parse_str('/MAX=', mparm_str);
	}
	set_global_str( str( menu_level ) + 'ISTR_1', return_str );
	set_global_int( str( menu_level ) + 'IINT_1', return_int );
	set_global_str( str( menu_level ) + 'IPARM_1', '/C=1/TP=' + str(numeric) +
									'/T=' + Parse_Str('/P=',MParm_Str) +
									'/W=' + Parse_Str('/W=',MParm_Str) +
									'/ML=' + parse_str('/ML=', mparm_str) +
									temp_str +
									'/HISTORY=' + parse_str('/HISTORY=' , mparm_str)
									);
	RM('DATA_IN /A=2/T=' + Parse_Str('/T=',MParm_Str) +
							'/PRE=' + str( menu_level ) +
							'/#=1/X=' + str( x ) +
							'/Y=' + str( y ) +
							'/H=' +Parse_Str('/H=',MParm_Str) +
							'/NK=' +Parse_Str('/NK=',MParm_Str));
	if(  Return_Int  ) {
		if(  Numeric  ) {
			return_int = global_int( str( menu_level ) + 'IINT_1' );
			Return_Str = 'TRUE';
		} else {
			return_str = global_str( str( menu_level ) + 'ISTR_1' );
		}
	} else {
		if(  Numeric  ) {
			Return_Str = 'FALSE';
			Return_Int = Tint;
		}
	}

	set_global_str( str( menu_level ) + 'ISTR_1', '' );
	set_global_int( str( menu_level ) + 'IINT_1', 0 );
	refresh = old_refresh;
	explosions = texp;
	Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') - 1);
}

macro VERIFY TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:  VERIFY

Description:  Creats a simple CONFIRM YES/NO box.

Parameters:
							/H=str  help string.
							/T=str  convirm message
							/S=nn   Starting item (0 = Yes, 1 = no );
							/BL=str Box label
							These 2 parameters are not normally neccesary because the box
							will automatically position itself.
							/C=nn column of upper left corner of box
							/L=nn line of upper left corner of box

Returns:      RETURN_INT = True if YES was selected,
													 False if NO was selected or ESC was pressed.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str  tstr[80] ;
	int  tl ;

	var_parse_str( '/T=', mparm_str, tstr );
	tl = svl(tstr);
	set_global_str('VFYIPARM_1','/TP=10/L=1/C=1/T=' + tstr );

	set_global_str('VFYIPARM_2','/TP=11/L=3/T= Yes /QK=2/R=1/W=5' );
	set_global_str('VFYIPARM_3','/TP=11/L=3/T= No /QK=2/R=0/K1=27/K2=1/W=4' );
	set_global_str('VFYIPARM_4','/TP=11/L=3/T= Help/KC=<F1> /K1=0/K2=59/R=2/QK=2/W=10' );
	if(  tl < 21  ) {
		tl = 21;
	}
	RM('CHECKEVENTS /M=4/G=VFYIPARM_/#=4/S=1/UC=1/X=1/Y=3/TW=21/W=' + str(tl));

	tstr = parse_str('/BL=', mparm_str);
	if(  tstr == ''  ) {
		tstr = 'Please Confirm';
	}

	RM('DATA_IN /#=4/PRE=VFY/S=' + str( 2 + parse_int('/S=', mparm_str )) +
			'/T=' + tstr + '/H=' + parse_str('/H=', mparm_str) +
			'/X=' + Parse_Str('/C=',MParm_Str) +
			'/Y=' + Parse_Str('/L=',MParm_Str));
}

macro CHECKFILE TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: CHECKFILE

Description:  Checks to see if a file has been saved and prompts the user if
							he wants to save before the window gets erased or deleted.  Will
							save the file, if he so chooses.

Returns:
							Return_Int
								0 - Don't destroy the data.
								1 - O.K. to blast it.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int tl;
	str TStr[80];
	return_int = TRUE;
	if( global_str('@DEL_VERIFY_MACRO') != '') {
		rm( global_str('@DEL_VERIFY_MACRO'));
	}
	if(return_int) {
		if(  (File_Changed) && (link_stat == 0)  ) {
			if(global_int('@DIALOG_STYLE'))
			{
				int menu = menu_create;
			 	menu_set_item(menu,1, "Save changes?","","/C=2/L=2",10, 0, 0 );
				menu_set_item(menu,2, " Yes ","","/QK=2/C=2/L=4/R=3",11, 0, 0 );
				menu_set_item(menu,3, " No  ","","/QK=2/C=9/L=4/R=1",11, 0, 0 );
				menu_set_item(menu,4, "Cancel","","/KC=<ESC>/C=16/L=4/R=0",11, 0, 0 );
				menu_set_item(menu,5, "Help","","/KC=<F1>/QK=1/C=36/L=4/R=2",11, 0, 0 );
				menu_set_item(menu,6, file_name,"","/C=2/L=7", 10, 0, 0 );

				return_int = menu;
				rm("DATA_IN /HN=1/T=File has been modified!/S=2/#=6/H=" +
										parse_str('/H=', mparm_str));
				menu_delete(menu);
			}
			else
			{


				tstr = file_name + ' has been modified';
				tl = svl(tstr);
				set_global_str('VFYIPARM_1','/TP=11/L=2/T= No /QK=2/R=0/K1=27/K2=1/W=4' );
				set_global_str('VFYIPARM_2','/TP=11/L=2/T= Yes(abandon changes) /QK=2/R=1/W=22' );
				set_global_str('VFYIPARM_3','/TP=11/L=2/T= Save file /R=3/QK=2/W=11' );
				set_global_str('VFYIPARM_4','/TP=10/L=4/C=1/T=' + tstr );
				if(  tl < 37  ) {
					tl = 37;
				}
				RM('CHECKEVENTS /M=4/G=VFYIPARM_/#=3/S=0/UC=1/X=1/Y=3/TW=38/W=' + str(tl));

				RM('DATA_IN /#=4/PRE=VFY/S=3/T=FILE NOT SAVED - CONTINUE?/H=' +
										parse_str('/H=', mparm_str)  );
			}
			if(  return_int == 3  ) {
				Error_Level = 0;
				rm("SAVEFILE /NP=1");
				// save_file;
				// return_int = 1;
				if(  error_level != 0  ) {
					RM('MEERROR');
					return_int = 0;
				}
			}
		}
	}
}


macro MAINHELP TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:  MAINHELP

Description:  Brings up the main help screen ME.HLP or ME.HLC.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	RM('MEHELP /F=ME/LK=*/CX=' + str(mode == EDIT));
}

#include userin1.s
#include userin2.s
