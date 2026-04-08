//+++keyword+++ "@(#)%v %n, %f, %w"
// "@(#)4 USERIN1.S, 1-Oct-93,0:55:38, LDH"

/*******************************************************************************
														MULTI_EDIT MACRO FILE

Name: USERIN  <-  USERIN1

Description:  - - Included by USERIN.S - -
							Most of the general purpose user input routines.

DATA_IN        - A general purpose Boxed Data entry menu.
DI_STRING_IN   - String input field handler for DATA_IN only.
DI_LIST_FIELD  - List field input handler for DATA_IN only.
DI_FKEY_ROW    - Funtion key row help handler for DATA_IN only.
DI_DIR_FIELD   - Directory field handler for DATA_IN only.
DI_EDIT_WINDOW - Edit window field handler for DATA_IN only.
HISTORY_LIST   - History list handler for DATA_IN only.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

str _specChar(str tstr[2048])  trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

NAME:  _SpecChar

DESCRIPTION:  This is a general purpose string manipulator that changes
certain "unprintable" characters to the Multi-Edit macro language ASCII
Character representation for the purpose of displaying them in prompts.  The
string is passed to this macro via the standard ME parameter passing
convention, and the result is returned.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str TChars[40];
	char TChar;
	int JX,Jy;

	Tchars = '|26|0|9|255|13|27';

	Jy = 1;
	while(  (Jy < 7)  ) {
		jx = 1;
		while(  (Jx > 0)  ) {
			jx = XPOS(Copy(TChars,Jy,1),TStr,jx);
			if(  jx != 0  ) {
				TStr = Str_Del(TStr,jx,1);
				TStr = Str_Ins('||' + Str(Ascii(Copy(TChars,Jy,1))),TStr,jx);
				jx = jx + 3 + (Jy == 4) - (Jy == 2);
/* Special instance of numeric characters following altered string */
SPECIAL_CASE:
				if(  (Length(Tstr) >= Jx)  ) {
					TChar = Copy(TStr,Jx,1);
					if(  (XPos(TChar,'0123456789',1))  ) {
						TStr = Str_Del(TStr,jx,1);
						TStr = Str_Ins('||' + Str(Ascii(Tchar)),TStr,Jx);
						Jx = Jx + 3;
						Goto SPECIAL_CASE;
					}
				}
			}
		}
		++Jy;
	}
	return(Tstr);
}


str _valChar(str tstr[2048]) trans2 {
	str Tstr2[60];
	int JX,Jy;
	char  TChar ;
	/* Get bar characters */
	JX = 1;
CHECKQ2:
	jx = XPOS('||',TStr,jx);
	if(  jx != 0  ) {
		if(  (JX < Length(TStr)) &
			 (XPOS(Copy(TStr,jx+1,1),'0123456789',1) != 0)  ) {
			jy = jx + 1;
			TStr2 = '';
Next_Char:
			TChar = Copy(Tstr,jy,1);
			if(  XPOS(TChar,'0123456789',1)  ) {
				TStr2 = TStr2 + TChar;
				++jy;
				Goto Next_Char;
			}
			if(  VAL(jy,Tstr2) == 0  ) {
				TStr = Str_Del(TStr,jx,Length(Tstr2) + 1);
				TStr = Str_Ins(Char(Jy),TStr,jx);
			}
			++jx;
		} else {
			jx = jx + 2;
		}
		goto CHECKQ2;
	}
	return(TStr);
}

macro DATA_IN trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:  DATA_IN

Description: Builds a screen of editable fields.  Each field has its own
	column and line setting, as well as its own help and type attributes.

Parameters:  /#=count  			// the number of fields
						 /S=start_field // the starting field number
						 /PRE=str 			// The prefix to use for the global var names
						 /X=nn  				// The Upper Right hand column, or x coordinate
						 /Y=nn  				// The Upper Right hand row, or y coordinate
						 /H=str  				// help string
						 /HT=nn  				// The height of the box, automatically calculated
															 if not present
						 /W=nn  				// The width of the box, automatically calculated
															 as above
						 /T=title  			// The title of the box
						 /A=nn  Accept type.
										0 = Accept or Cancel.
										1 = Accept no matter what
										2 = Force use of enter key for accept (applies only
												 to classic mode);
										3 = Enter key is Go, F10 key is Done, (applies only to
												 classic mode);
										4 = Esc key is DONE;

						 /NC=nn 1 = NO Cleanup.  Don't erase global variables when done.
						 /NB=1  Don't create a box
						 /NK=1  Don't kill box
						 /NM=1  Don't allow box move.
						 /VA=1  Verify abort.  If user presses cancel or abort, bring up
										a verify prompt.

						 /EDITFIELDS=1  a very special mode which allows one to move fields
														around.  Does not allow user input.


Returns:    RETURN_INT = 0 if the <ESC> key was pressed.
						Else RETURN_INT = Then item that <ENTER> was pressed on.

Global Vars:
				ISTR_1 .. ISTR_x   	// field string, x = count
				IPARM_1 .. IPARM_x  // parmaeter string,x = count
					/C=       column
					/L=       line
					/W=       displayable width of field
					/ML=      max length if max length is 0 then max length = width.
					/H=       help_link.
					/T=       Field title
					/GO=      If <enter> pressed on this field, treat like accept field.
					/G=       Name of a global string to use instead of ISTR_x - rarely
										used
					/HISTORY= name of history_list globals
					/PROTECT= 1 prevents a field from being changed.
					/MIN=     Minimum numeric value if a numeric type
					/MAX=     Maximum numeric value if a numeric type
					/NHT=     No Height adjust.
					/TP=      type
										0 = string (default)
										1 = integer
										2 = real number
										3 = Multiple Choice with vertical menu.
												ISTR_x contains the menu.
												IINT_x contains the choice number.
										4 = Hex - same as integer, except display and
															user input in hex.
										5 = toggle true or false.
												ISTR_x = '/T=YES/F=NO'
												IINT_x = boolean value.
										6 = Not used...
										7 = Run macro, return integer.
												IPARM=x /M=macro (must be last parameter)
												The following parameters get passed to the
												macro:
													/X=nn
													/Y=nn
													/STR=str (the string in ISTR_x)
										8 = Run macro, return string(or return global, see below).
												IPARM=x /M=macro (must be last parameter)
													/X=nn
													/Y=nn
													/INT=nn (the integer in IINT_x)
										/RGS= Return global string.  Used only with /TP=8.
													Due to the 255 character limit on Return_Str, if
													you specify the name of a global string, it will
													use that instead.

										9 = Keycode field.  It is stored in IINT as an integer
												lower byte is primary scan code, upper byte is
												extended scan code.  Field will be displayed as in
												the example below:
													SET_GLOBAL_INT('IINT_1',7181);
													Displays <ENTER>
												User will be prompted for a keystroke when he presses
												<ENTER> a type 9 field.
									10 = Display data, but no entry field.  Uses /L= only.
													/ATTR=nn  attribute
									11 = Button.
									12 = Radio box
									13 = Check box
									15 = List box
												/WIN=nn  	Window number, NOT WINDOW_ID!!!!!!!!!
												/HT=nn   	height in lines
												/OL=1			Only list no top/bottom border on box
												/DC=1			Enables mouse double click to accepts
																	dialog.
												/MRK=1		  Enable marking
												/DISPMAC=macro (must be last parameter)
																	Macro run after processing a key.
																	Used to update the dialog display
																	while in a list box.

												The following are used only if /TAG=1 in a
												directory box is specified.
													/NTM=1		Disable showing tag/untag message on
																	message line.
													/TAGMAC=macro (must be last parameter)
																	This macro is call when a file is
																	tagged and returns a string that is
																	appended to the tagged filename.  Used
																	to enable file type or wrkdir to be
																	stored in tag list.

									16 = Edit window box
											/WIN=nn  Window number
											/HT=nn   height in lines
											/W=nn  Width of list
											/PB=n non-zero persistent blocks on
											/OL=1		 Only list no top/bottom border on box
											/DC=1		 Enable mouse double click to accepts dialog
									17 = Hidden field
									20 = Directory box
											/MSK=#	menu # of file mask string
											/SDD=# 	menu # of directory tree list box or 0 for none.
											/TAG=#  menu # of file tag list box or 0 for no tag list.


				IINT_x = value for integer or multiple choice.

		Note that if an X and Y coordinate was specified in the parameter line
		then, the column and line numbers will be offsets from the X and Y
		coordinates.


Example:  // Setup multi-file search

		Set_Global_Str('ISTR_1',filespec);
		Set_Global_Str('IPARM_1','/T=Filespec:/C=1/W=60/ML=80/H=SR/L=1');
		Set_Global_Str('ISTR_2',Search_Str);
		Set_Global_Str('IPARM_2','/T=Search For:/C=1/W=63/ML=128/H=SR/L=2');
		Set_Global_Str('IPARM_3','/T=>>>>/C=1/H=SR/L=3/TP=6/W=17');
		Set_Global_Str('ISTR_3','START FILE SEARCH');

		Set_Global_Int('IINT_4',search_dirs);
		Set_Global_Str('ISTR_4','/T=YES/F=NO');
		Set_Global_Str('IPARM_4','/T=Search Subdirectories..../C=1/W=3/H=SR/L=4/TP=5');
		Set_Global_Str('ISTR_5',global_str('FSEARCH_PATH'));
		Set_Global_Str('IPARM_5','/T=Starting path............/C=1/W=40/H=SR/L=5');
		Set_Global_Int('IINT_6',case_sensitive);
		Set_Global_Str('ISTR_6','/T=YES/F=NO');
		Set_Global_Str('IPARM_6','/T=Case Sensitivity........./C=1/W=3/H=SR/L=6/TP=5');
		Set_Global_Int('IINT_7',use_reg_exp);
		Set_Global_Str('ISTR_7','/T=YES/F=NO');
		Set_Global_Str('IPARM_7','/T=Use Regular Expressions../C=1/W=3/H=SR/L=7/TP=5');

		RM('UserIn^Data_In /S=1/A=3/#=7/X=1/T=SEARCH FILES/Y=' + str(y1));


							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/


#DEFINE CHECK_RADIO_WIDTH 5
#DEFINE RADIO_BUTTON_ON   ' (|7) '
#DEFINE RADIO_BUTTON_OFF  ' ( ) '
#DEFINE CHECK_BOX_ON      ' [X] '
#DEFINE CHECK_BOX_OFF     ' [ ] '
#DEFINE SHADOW_BUTTONS    TRUE

/*
#DEFINE CHECK_RADIO_WIDTH  3
#DEFINE RADIO_BUTTON_ON   ' |16 '
#DEFINE RADIO_BUTTON_OFF  ' |9 '
#DEFINE CHECK_BOX_ON      ' |251 '
#DEFINE CHECK_BOX_OFF     ' _ '
#DEFINE SHADOW_BUTTONS    TRUE
 */

#DEFINE dir_immediate Global_Int('DIR_IMMEDIATE')

	str mstr,
			Prefix[10],
			iint[15], iparm[15], istr[15],
			Label_Str[80],
			c_str1[20],
			RGS[20],
			signal_str[20] = parse_str('/SGNL=', mparm_str)
			;          /* General purpose string */


	char tchar ;
	int	tcp,
			fjx,
			menu,
			use_handle = parse_int('/HN=', mparm_str ),
			t_refresh = Refresh,
			field_count,    /* The number of fields */
			fc, bc,         /* Foreground and background colors */
			X1, Y1, Height,
			Width, jx, jy, jz, old_x, old_y,
			c_choice,
			tc, tl, tw,tt,  /* Temp col, line, and width, type */
			ll,             /* Temp label length */
			protect,        /* field protect status */
			hc, hl,         /* Help line and column */
			C_Parm,         /* The parm string being worked on */
			old_c, old_l,    /* The old cursor position */
			accept_mode,
			history_stat,
			tmw,
			full_write,
			mouse_result,
			count,
			Edit_Enable = FALSE,
			go_accept = FALSE,
			t_parm = 0,
			button_line,
			ok_button = 0,
			cancel_button = 0,
			help_button = 0,
			box = (parse_int('/NB=', mparm_str) == 0),
			xx, yy,
			highlighted = 0,
			group_attr = cb_t_color,
			menu_count,
			move = 0,

			dir_field_found = 0,
			dir_field_active,
			orig_mode = mode,
			btn_c, bk_c,
			old_fkey_row = fkey_row,
			Fkey_Set = 0,
			old_win = cur_window,
			Item_marked = 0,
			button_down = 0,
			dialog_style = GLOBAL_INT('@DIALOG_STYLE'),
			history_stat,
			Display_Lines = 0,
			signal_flag,
			dir_sd,
			t_dsa = dir_search_attr,
			edit_fields = parse_int("/EDITFIELDS=",mparm_str), /* stuff for moving fields */
/* to enable special wildcard in file prompts flag */
			wildcard_mode = (parse_int("/WM=", mparm_Str) &&
													global_int("@WILDCARD_MODE")),
			tag_sd,
			tag_update = TRUE,
			dir_msk_field
			;

		real t_real;

	if (use_handle) {
		menu = return_int;
	}
	else {
		menu = menu_create;
	}
/*
	if(  (Mode == Term)  ) {
		Old_FKey_Row = Global_Int('COM_STATUS_ROW');
	}
*/
	signal_flag = (signal_str != "");

	refresh = FALSE;
	working;
	Var_Parse_Str('/RGS=',MParm_Str, Rgs);

	old_c = WhereX;          /* Save the cursor position for later restoration */
	Old_l = WhereY;

	Return_Int = False;
	field_count = 0;
	Push_Labels;
	Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') + 1);

		/* Get the count of the fields */
	var_parse_int( '/#=', mparm_str, field_count );

	var_parse_str('/POSG=', mparm_str, mstr );
	if(  mstr != ''  ) {
		mstr = global_str( mstr );
		Var_Parse_Int('/X=', mStr, x1);
		Var_Parse_Int('/Y=', mStr, y1);
	} else {
		Var_Parse_Int('/X=', MParm_Str, x1);
		Var_Parse_Int('/Y=', MParm_Str, y1);
	}

	var_parse_str(  '/PRE=', mparm_str, prefix );
	iint = prefix + 'IINT_';
	istr = prefix + 'ISTR_';
	iparm = prefix + 'IPARM_';

	if(  NOT( use_handle )  ) {
		call reset_fields;
		jx = 0;
		while(  jx < field_count  ) {
			++jx;
			mstr = global_str( iparm + str(jx ) );
			menu_set_item( menu, jx,
				parse_str('/T=', mstr) ,global_str(istr + str(jx)),mstr ,
				parse_int('/TP=',mstr), global_int( iint + str(jx)), 0 );
		}
	}

REBUILD_BOX:
	height = 0;
	width = 0;
	var_parse_int( '/A=', mparm_str, accept_mode );

	if(  (dialog_style) & (accept_mode == 3)  ) {
		accept_mode = 0;
	}
	if(  (accept_mode == 0) & (field_count == 1)  ) {
		accept_mode = 2;
	}
	if(( accept_mode == 4) && !(dialog_style))
	{
		accept_mode = 1;
	}
	if(accept_mode == 4)
	{
		ok_button = TRUE;
	}
	button_line = height - 1;

		/* If no height or width then calculate then by going through the
		 entire list of fields and determining the maximun column and
		 row needed. */
		C_Parm = 1;
		while(  (C_Parm <= field_count)  ) {
			MStr = menu_item_str( menu,  C_Parm  , 3);
			Var_Parse_Int('/L=',MStr, jy);
			menu_set_int( menu, c_parm, 3, jy );
			tt = menu_item_int( menu, c_parm, 1 );
			label_str = menu_item_str( menu, c_parm, 1 );
			jx = svl(label_str);
			var_parse_int('/C=', mstr, xx);
			if(  (tt != 17)  ) {
				switch(tt) {
				case 12 :
					jx = jx + xx + CHECK_RADIO_WIDTH;
					if(  menu_item_int( menu, c_parm, 2 ) > 0  ) {
						var_parse_str( '/G=', mstr, c_str1 );
						if(  svl(c_str1) > 0  ) {
							var_parse_int( '/R=', mstr, ll );
							set_global_int( c_str1, ll );
						}
					}
					break;

				case 13 :
					jx = jx + xx + CHECK_RADIO_WIDTH;
					break;

				case 11  :  /* if button */
					var_parse_str( '/KC=', mstr, label_str );
					jx = jx + xx + svl( label_str ) + SHADOW_BUTTONS ;
					var_parse_int('/R=', mstr, ll );
					if(  ll == 0  ) {
						cancel_button = TRUE;
					} else if(  ll == 1  ) {
						ok_button = TRUE;
					} else if(  ll == 2  ) {
						help_button = TRUE;
					}
					if(jy != 0) {
						++jy;
					}
					break;

				case 16 :
					switch_window( parse_int('/WIN=', mstr) );
					var_parse_int( '/W=', mstr, jx );
					if(  jx == 0  ) {
						jx = 20;
					}
					Jx = Jx + xx + 1;
					var_parse_int( '/HT=', mstr, ll );
					if(  ll == 0  ) {
						ll = 3;
					}
					jy = jy + ll;
					break;

				case 15 :
					display_lines = parse_int("/DISP#=", mstr);
					switch_window(parse_int("/WIN=", mstr));
					call calculate_list_field;
					var_parse_int("/HT=", mstr, jx);
					if (jx == 0) {
						jx = hc + 1;
						var_parse_int( '/MH=', mstr, tc );
						if(  tc == 0  ) {
							tc = 2;
						}
						if(  jx < tc  ) {
							jx = tc;
						}
						mstr = mstr + '/HT=' + str(jx);
					}
					jy = jy + jx + Display_Lines - 1;
					size_window( win_x1, min_window_row, win_x2, min_window_row + jx );
					var_parse_int( '/W=', mstr, jx );
					if(  jx == 0  ) {
						jx = ll;
						var_parse_int( '/MW=', mstr, ll );
						if(  (ll == 0)  ) {
							ll = 15;
						}
						if(  jx < ll  ) {
							jx = ll;
						}
						mstr = mstr + '/W=' + str(jx);
					}
					var_parse_int('/OR=', mparm_str, ll);
					if(  ll == 0  ) {
						ll = c_row;
					}
					if(  c_row != ll  ) {
						if(  hc > ( win_y2 - win_y1 - 1 )  ) {
							hc = win_y2 - win_y1 - 1;
						}
						tc = c_line;
						TOF;
						if(  ll > menu_count  ) {
							ll = menu_count;
						}
						--hc;
						while(  (c_row < ll) & (c_row < hc)  ) {
							DOWN;
						}
						Goto_Line(tc);
					}
					menu_set_str(menu, c_parm, 3, mstr);
					menu_set_int(menu, c_parm, 2,  menu_count);
					Jx = xx + jx + 1;
					break;
				case 0 :
				case 1 :
					if(  (tt == 0)  ) {
						if(  (Parse_Str('/G=',Mstr) != '')  ) {
	/* If they wish to use a specified storage global, use that instead
	10-03-90 TR */
							menu_set_str( menu, c_parm, 2, Global_Str(Parse_Str('/G=',Mstr)));
						}
						if(  (Parse_Int('/SPCH=',MStr))  ) {
	/* Convert non printable characters into |xx on string type only if required.
	This was primarily created for the printer driver menu */
							menu_set_str( menu, c_parm, 2, _SpecChar(menu_item_str( menu, c_parm, 2 )) );
						}
					} else if(  (tt == 1)  ) {
						if(  (Parse_Str('/G=',Mstr) != '')  ) {
	/* If they wish to use a specified storage global, use that instead
	10-03-90 TR */
							menu_set_int( menu, c_parm, 2, Global_Int(Parse_Str('/G=',Mstr)));
						}
					}
					var_parse_int( '/W=', mstr, ll );
					Jx = Jx + xx + ll;
					if(  xpos('/HISTORY=', mstr, 1)  ) {
						jx = jx + 3;
					}
					++jx;
					break;

        case 20 :
          dir_field_found = c_parm;
          if ((jy + 15) > height) {
            height = jy + 15;
          }
          jx = xx + 46;
          var_parse_int("/MSK=", mstr, dir_msk_field);
          var_parse_int("/SDD=", mstr, dir_sd);  // directory tree window
          var_parse_int("/TAG=", mstr, tag_sd);  // tag list window
          if (dir_sd) {
            dir_search_attr = 0x27;
            if (dir_msk_field == 0) {
              dir_sd = dir_field_found + 1;
            }
          }
          if (dir_msk_field == 0) {
            dir_msk_field = dir_field_found - 1;
          }
          return_int = 0;               // Initialize field
          call dir_field_macro;
          break;

				default :
					var_parse_int("/W=", mstr, ll);
					Jx = Jx + xx + ll;
					if(  tt != 10  ) {
						++jx;
						if(  (tt >= 3)  ) {
							jx = jx + 3;
						}
					}
				}
				if(  jy == 0  ) {
					jy = height + 1;
					++height;
					if( tt == 11 )
						++height;
					menu_set_int( menu, c_parm, 3, jy );
					if(  NOT( use_handle )  ) {
						set_global_str( iparm + str(c_parm), mstr + '/L=' + str(jy) );
					}
				} else if(  jy > Height  ) {
					if(  parse_int('/NHT=', mstr) == 0  ) {
						Height = Jy;
					}
				}
				if(  jx > width  ) {
					width = jx;
				}
			}
			++C_Parm;
		}
		if(  (parse_int('/DBM=', mparm_str) == 0) &
				((((ACCEPT_MODE == 0) | (ACCEPT_MODE == 2))
						& (cancel_button == 0)) |
					NOT(ok_button) | NOT(help_button))  ) {
			if(  height < (screen_length - 1 - (fkey_row != 0) - (menu_bar_row != 0) - (message_row != 0))  ) {
				++height;
			}
			if(  height < (screen_length - 2 - (fkey_row != 0) - (menu_bar_row != 0) - (message_row != 0))  ) {
				++height;
			}
			tw = 0;
			var_parse_str('/ABT=', mparm_str, c_str1 );
			if(  c_str1 == ''  ) {
				if(  accept_mode == 3  ) {
					c_str1 = 'GO';
				} else {
					c_str1 = 'OK';
				}
			}
			var_parse_str('/CBT=', mparm_str, Mstr );
			if(  mstr == ''  ) {
				if(  (accept_mode == 3) || (accept_mode == 4)  ) {
					mstr = 'Done';
				} else {
					mstr = 'Cancel';
				}
			}
			if(  not(ok_button)  ) {
				if(  (dialog_style) | (accept_mode == 2) | (accept_mode == 3)  ) {
					tw = svl( c_str1 ) + 8 + shadow_buttons ;
				} else {
					tw = svl(c_str1) + 6 + shadow_buttons ;
				}
			}
			if(  not(cancel_button) & ((accept_mode == 0) | (accept_mode == 2) | (accept_mode == 3) | (accept_mode == 4))  ) {
				tw = tw + Svl(Mstr) + 6 + shadow_buttons ;
			}
			if(  not(help_button)  ) {
				tw = tw + 9 + shadow_buttons ;
			}
			tw = tw - 1;
			if(  width < tw  ) {
				width = tw;
			}
			tw = (width / 2) - (tw / 2);
			if(  tw < 1  ) {
				tw = 1;
			}
			if(  NOT( ok_button )  ) {
				++field_count;
				if(  (dialog_style) | (accept_mode == 2) | (accept_mode == 3)  ) {
					menu_set_item( menu, field_count, c_str1, '', '/R=1/KC=<ENTER>/K1=13/K2=28/C=' + str(tw), 11, 0, height );
					tw = tw + svl(c_str1) + 8 + SHADOW_BUTTONS ;
				} else {
					menu_set_item( menu, field_count, c_str1, '','/R=1/KC=<F10>/K1=0/K2=68/C=' + str(tw),11,0,height );
					tw = tw + svl(c_str1) + 6 + SHADOW_BUTTONS ;
				}
			}
			if(  NOT( cancel_button ) & ((accept_mode == 0) | (accept_mode == 2) | (accept_mode ==3) | (accept_mode == 4))  ) {
				++field_count;
				if(  accept_mode == 3  ) {
					menu_set_item( menu, field_count, mstr, '','/R=0/KC=<F10>/K1=0/K2=68/C=' + str(tw), 11, 0, height );
				} else {
					menu_set_item( menu, field_count, mstr, '','/R=0/KC=<ESC>/K1=27/K2=1/C=' + str(tw), 11, 0, height );
				}
				tw = tw + Svl(mstr) + 6 + SHADOW_BUTTONS ;
			}
			if(  NOT( help_button )  ) {
				++field_count;
				menu_set_item( menu, field_count, 'Help', '','/R=2/KC=<F1>/K1=0/K2=59/C=' + str(tw), 11, 0, height );
				tw = tw + 9 + SHADOW_BUTTONS ;
			}
			if(  height < (screen_length - (fkey_row != 0) - (menu_bar_row != 0) - (message_row != 0))  ) {
				++height;
			}
//    	if(  height < (screen_length - 1 - (fkey_row != 0) - (menu_bar_row != 0) - (message_row != 0))  ) {
//      	++height;
//			}
			tw = tw - 1;
			if(  width < tw  ) {
				width = tw;
			}
		}
		if(  height < (screen_length - (fkey_row != 0) - (menu_bar_row != 0) - (message_row != 0))  ) {
			++height;
		}
		if(  height < (screen_length - (fkey_row != 0) - (menu_bar_row != 0) - (message_row != 0))  ) {
			++height;
		}
/* This will take into account the width of the box label */
		Jx = Length(Parse_Str('/T=',Mparm_Str));
		if(  (Jx > Width)  ) {
			Width = Jx;
		}
		width = width + 2;

		var_parse_int( '/HT=', mparm_str, jx );
		var_parse_int( '/W=', mparm_str, jy );

		if(  jx != 0  ) {
			height = jx;
		}
		if(  jy != 0  ) {
			width = jy;
		}

	Call Build_Dialog_Box;

	var_parse_int( '/S=', mparm_str, C_Parm );  /* Get the starting field number */
	if(  c_parm == 0  ) {
		c_parm = 1;
	}

	call not_10;
	if(  (tt == 12)  ) {
		call find_radio_box;
	}

/* BEGIN STUFF FOR MOVING FIELDS */
	if (edit_fields) {
		rm("di_edit_fields /H=" + str(menu) + "/#=" + str(field_count)
				+ "/X=" + str(X1) + "/Y=" + str(Y1));
		if (return_int == 0) {
			goto EXIT;
		}
		kill_box;
		var_parse_int( '/#=', mparm_str, field_count );
		goto REBUILD_BOX;
	}
/* END STUFF FOR MOVING FIELDS.  Also can remove label REBUILD_BOX,
and variable EDIT_FIELDS */


	/* make_message( str( system_timer - timer ) ); */

main_loop:
	call check_c_parm;
	call mark_item;
	t_parm = c_parm;
	old_x = 0;
	old_y = 0;
	call write_fkey_row;
	if (tt == 16) {                       // edit window box
		call process_edit_field;
		if (return_int == 10) {
			goto xgot_key;
		}
	}
	while (!check_key) {
		mou_check_status;
		if (((mou_last_status & 1) != 0) && ((old_x != mou_last_x) ||
																				 (old_y != Mou_last_Y))) {
			old_x = mou_last_x;
			old_y = mou_last_y;
			call find_mouse;
		}
		if (signal_flag) {
			jx = global_int(signal_str);
			if (jx > 0) {
				return_int = jx;
				goto exit;
			}
		}
	}

xgot_key:
	if ((key1 == 0) && (key2 == 68)) {    // <F10>
		if ((dialog_style == 0) && (accept_mode == 2)) {
			key1 = 13;
			key2 = 28;
		}
	}
	if ((key1 == 13) && (key2 == 224)) {  // <GrayENTER>
		key2 = 28;
	}
	if (key1 == 0) {
		if (key2 == 162) {                  // <AltIns>
			rm("mehelp^screenMrk");
			goto main_loop;
		}
		// This allows insertion of a null character
		if (key2 == 3) {                    // <Ctrl@>
			goto editable_field;
		}
		if ((key2 == 251) && tag_sd) {      // <MEVENT2> and TAG
			tag_update = FALSE;
			call find_mouse;
			if (mouse_result) {
				if (tt == 20) {                 // Directory box, tag file
					call process_dir_field;
				}
				else if ((tt = 15) && (c_parm == tag_sd)) {  // Tag list box, del tag
					key2 = 250;                   // set key to <MEVENT>
					call process_list_field;
					tag_update = TRUE;
					key2 = 83;                    // set key to <DEL>
					goto editable_field;          // process key for list box
				}
			}
			tag_update = TRUE;
		}
		else if (key2 == 250) {             // <MEVENT>
			if (mou_last_y == y1) {
				if ((mou_last_x >= x1) && (mou_last_x <= (x1 + width))) {
					return_int = 0;
					call move_box;
				}
			}
			else {
				call find_mouse;
				if (mouse_result) {
					if (tt == 11) {               // Button
						button_down = TRUE;
						call write_istr;
						button_down = FALSE;
					}
					if ((tt != 3) && (tt != 20) && (tt != 15) && (tt != 16)) {
					// Not Multiple choice or Directory box or List box or Edit window
						int ox = mou_last_x,
								oy = mou_last_y;

hi_loop:
						int timex = system_timer;

						mou_check_status;
						while ((mou_last_status & 1) && ((system_timer - timex) < 3))
								mou_check_status;
						if (mou_last_status & 1) {
							if ((mou_last_x != ox) || (mou_last_y != oy)) {
								if ((mou_last_x > (x1 + tc + ll + tw)) ||
										(mou_last_x < (x1 + tc)) || (mou_last_y != (y1 + tl)))
								{
									call unmark_item;
									goto no_mouse_process;
								}
							}
							goto hi_loop;
						}
					}
					switch (tt) {
					case 3:
						call input_menu_field;
						break;

					case 5:
						call toggle_field;
						break;

					case 7:
						call run_int_macro;
						break;

					case 8:
						call run_str_macro;
						break;

					case 9:
						call input_quickstroke;
						break;

					case 13:
						call check_box;
						break;

					case 12:
						call radio_box;
						break;

					case 11:
						goto process_button;
						break;

					case 20:
						if (dir_field_active) {
							call process_dir_field;
						}
						break;

					case 15:                      // List box
						call process_list_field;
						break;

					case 0:
					case 1:
					case 2:
					case 4:
						if (t_parm == c_parm) {
							goto editable_field;
						}
						if ((key1 != 0) || (key2 != 250)) {  // key not <MEVENT>
							push_key(key1, key2);
						}
						break;

					case 16:                      // Edit window
						push_key(key1, key2);
					}
no_mouse_process:
				}
			}
		}
		else if (key2 == 59) {              // <F1> (help)
do_help:
			call get_help;
		}
		else {
			return_int = -1;
			if (tt == 15) {                   // List box
				call process_list_field;
			}
			else if (tt == 20) {              // Directory box
				if (xpos(char(key2), '|71|72|73|75|77|79|80|81|240|241|242|243|244', 1))
				{
					call process_dir_field;
				}
			}
			if (return_int == -1) {
				if (key2 == 15) {               // <ShftTAB>
					call previous_item;
				}
				else if (key2 == 80) {          // <DN>
					if (dialog_style) {
						if (tt == 0) {              // String
							if (history_stat) {
								call input_string_history;
							}
						}
						else if (tt == 3) {         // Multiple choice
							call input_menu_field;
						}
						else if (tt == 8) {         // Run macro
							call run_str_macro;
						}
																				// Check box, Radio button, Button
						else if ((tt == 13) || (tt == 12) || (tt == 11)) {
radio_box_down:
							call next_same;
							if (tt == 12) {           // Radio button
								call radio_box;
							}
						}
					}
					else {
						call next_item;
					}
				}
				else if (key2 == 72) {          // <UP>
// 07-26-90 10:46am added this to fix problem with SAA up arrow.  T.R.
					if (dialog_style) { 					// Check box, Radio button, Button
						if ((tt == 13) || (tt == 12) || (tt == 11)) {
							radio_box_up:
							call prev_same;
							if (tt == 12) {           // Radio button
								call radio_box;
							}
						}
					}
					else {
// 07-26-90 10:46am end of SAA up arrow fix
						call previous_item;
					}
				}
				// <RT> and (Check box or Radio button or Button)
				else if ((key2 == 77) && ((tt == 13) || (tt == 12) || (tt == 11))) {
					goto radio_box_down;
				}
				// (<LF> or <UP>) and (Check box or Radio button or Button)
				else if (((key2 == 75) || (key2 == 72)) &&
								 ((tt == 13) || (tt == 12) || (TT == 11))) {
						call prev_same;
						if (tt == 12) {             // Radio button
							call radio_box;
						}
				}
				// <RT> or <END> or <DEL> or <CtrlRT>
				else if ((key2 == 77) || (key2 == 79) || (key2 == 83) || (key2 == 116)) {
					goto check_buttons;
				}
				else if (key2 == 238) {    			// <ScrollLockOn>
					return_int = 1;
					call move_box;
				}
				// <F4> and (Multiple choice or Run macro int or Run macro str)
				else if ((key2 == 62) && ((tt == 3) || (tt == 7) || (tt == 8))) {
					goto select_field;
				}
				else {
					call check_quick_keys;
					if (return_int == 1) {
						goto select_field;
					}
					else if (return_int == 2) {
						goto main_loop;
					}
					goto check_buttons;
				}
			}
			goto main_loop;
		}
	}
	else if (key2 == 0) {
		goto editable_field;
	}
	else if ((key1 == 13) && (tt == 11)) {  // <ENTER> and Button
		goto process_button;
	}
	else if ((key1 == 9) && (key2 == 15)) {  // <TAB>

dir_wildcard:
		call next_item;
	}
	else if (key1 == 32) {                // <SPACE>
		if ((tt == 20) && tag_sd) {         // Directory box and tagging enabled
			call process_dir_field;
			goto main_loop;
		}
		if (tt == 0) {                      // String
			push_key(key1, key2);
			call input_string_field;
			goto main_loop;
		}
select_field:
		if (tt == 11) {                     // Button type
process_button:
			if (tt == 11) {                   // Button type
				button_down = true;
				call write_istr;
				button_down = FALSE;
			}
			if (parse_int("/M=", mstr)) {     // Run macro
				mstr =  menu_item_str( menu, c_parm, 2 );
				set_global_int("!DATA_IN_#", t_parm );
				if (xpos(" ", mstr, 1) == 0) {
					mstr = mstr + " ";
				}
				fkey_row = old_fkey_row;
				call set_fields;
				rm(mstr + "/PRE=" + prefix + "/BC=" + str(box_count)
						+ "/X1=" + str(x1) + "/Y1=" + str(y1)
						+ "/X2=" + str(x1 + width - 2) + "/Y2=" + str(y1 + height - 1));
				call reset_fields;
				c_parm = t_parm;
				t_parm = 0;
				call draw_all_fields;
				call mark_item;
			}
			else {
				return_int = parse_int("/R=", mstr );
				if (return_int == 2) {
					call get_help;
					c_parm = t_parm;
					t_parm = 0;
					call draw_all_fields;
					call mark_item;
					goto main_loop;
				}
// IF this dialog box contains a directory field than we must do special
// processing before allowing an OK
				if ((return_int == 1) && (dir_field_found > 0)) {

					int tpp = dir_field_found;

					if (dir_sd && (t_parm == dir_sd)) {
						tpp = t_parm;
						return_str = get_line;
						if (!((length(return_str) == 2) && (copy(return_str, 2, 1) == ':')))
						{
							return_str = get_path( dir_mask ) + return_str;
						}
						goto pb2;
					}
					else {
						working;
						return_str = fexpand(menu_item_str(menu, dir_msk_field, 2));
						if (parse_int("/DIR=", menu_item_str(menu, dir_field_found, 3))) {
							goto exit;
						}
						if (copy(return_str, length(return_str), 1) == "\\") {
							if (tag_sd && (menu_item_int(menu, tag_sd, 2) != 0)) {
								goto exit;
							}
							goto pb1;
						}
						if (xpos("*", return_str, 1) || xpos("?", return_str, 1)) {
							if (wildcard_mode) {
// This supports the configuration of displaying wildcards in dir instead of
// loading them
								call unmark_item;
								c_parm = dir_msk_field;
								goto dir_wildcard;
							}
							goto exit;
						}
						if (first_file(return_str) == 0) {
							if ((last_file_attr & 0x10) != 0) {
pb1:
								if (copy(return_str, length(return_str), 1) != "\\") {
									return_str = return_str + "\\";
								}
pb2:
								working;
								set_virtual_display;
								menu_set_str(menu, dir_msk_field, 2,
															return_str + truncate_path(dir_mask));
								c_parm = dir_field_found;
								t_parm = 0;
								call draw_all_fields;
								call mark_item;
								call unmark_item;
								return_int = 1;
								c_parm = tpp;
								update_virtual_display;
								reset_virtual_display;
								goto main_loop;
							}
						}
					}
				}
				c_parm = t_parm;
				goto exit;
			}
		}
		else {
			call go_select_field;
		}
	}
	else {

check_buttons:
		call locate_buttons;
		if (return_int > 0) {
			fjx = return_int;
			call unmark_item;
			c_parm = fjx;
			call mark_item;
			goto process_button;
		}
		if ((key1 == 27) && (key2 == 1)) {  // <ESC>
			return_int = 0;
			goto exit;
		}
		else if ((key1 == 13) && (key2 == 28)) {  // <ENTER>
			call process_cr;
			if (return_Int) {
				goto exit;
			}
			goto main_loop;
		}

editable_field:
		if (tt == 0) {                      // String
			push_key(key1, key2);
			call input_string_field;
		}
		else if (tt == 1) {                 // Integer
			push_key(key1, key2);
			call input_integer_field;
		}
		else if(  (tt == 2)  ) {            // Real
			push_key(key1, key2);
			call input_real_field;
		}
		else if(  (tt == 4)  ) {            // Hex
			push_key(key1, key2);
			call Input_Hex_Field;
		}
		else if ((tt == 5) || (tt == 13)) {  // T/F toggle, Check box
			if ((key1 == 121) || (key1 == 89) || (key1 == 43)) {  // y Y <Gray+>
				menu_set_int(menu, c_parm, 2,  1);
			}
			else if ((key1 == 110) || (key1 == 78) || (key1 == 45)) {  // n N <Gray->
				menu_set_int(menu, c_parm, 2,  0);
			}
			else {
				call quick_keys1;
				if (return_int) {
					goto select_field;
				}
			}
		}
// Delete quickstroke
		else if ((tt == 9) && (key1 == 0) && (key2 == 83)) {  // Keycode, <DEL>
			menu_set_int(menu, c_parm, 2,  0);
		}
		else if (tt == 12) {                // Radio
			call quick_keys1;
			if (return_int) {
				goto select_field;
			}
		}
		else if (tt == 11) {                // Button
			call quick_keys1;
			if (return_int) {
				goto select_field;
			}
		}
		else if (tt == 15) {                // List box
			if (c_parm == dir_sd) {
				if (((key1 == 127) && (key2 == 14)) ||  // <CtrlBS>
						((key1 > 0) && (key1 < 27) && (key2 > 0)))  // Ctrl a-z
				{
					c_parm = dir_field_found;
					call parse_item;
					call process_dir_field;
					call unmark_item;
					t_parm = 0;
					c_parm = dir_sd;
					call mark_item;
					goto main_loop;
				}
			}
			else if (c_parm == tag_sd) {
				if ((key1 == 0) && (key2 == 83)) {  // <DEL>
					c_parm = dir_field_found;
					call parse_item;
					set_global_int("@TAG_LIST_WIN", TRUE);
					call process_dir_field;
					set_global_int("@TAG_LIST_WIN", FALSE);
					if (menu_item_int(menu, tag_sd, 2)) {
						t_parm = 0;
						c_parm = tag_sd;
						call mark_item;
						call write_tag_entry;
					}
					goto main_loop;
				}
			}
			call process_list_field;
		}
		else if (tt == 20) {                // Directory box
			call process_dir_field;
		}
	}
	goto main_loop;

draw_all_fields:
	tcp = c_parm;
	c_parm = 0;
	fc = m_s_Color;
	item_marked = FALSE;
	group_attr = cb_t_color;
	btn_c = button_color;
	bk_c = button_key_color;
	bc = 0;
	while (c_parm < field_count) {
		++c_parm;
		call parse_item;
		if (tt == 15) {                     // List box
			switch_window(parse_int("/WIN=", mstr));
			call calculate_menu_count;
			menu_set_int(menu, c_parm, 2, menu_count);
		}
		call write_istr2;
	}
	c_parm = tcp;
	ret;

get_help:
	mstr = parse_str("/H=", menu_item_str(menu, c_parm, 3));
	if (mstr == "") {
		mstr = parse_str("/H=", mparm_str);
	}
	help(mstr);
	ret;

mark_item:
	group_attr = cb_h_color;
	btn_c = m_h_color;
	bk_c = m_h_color;
	fc = m_h_color;
	item_marked = true;
	bc = 0;
	call write_istr;
	if ((tt != 20) && (tt != 15)) {       // Not Directory box or list box
		if (tt != 11) {                     // Not Button
			gotoxy(x1 + tc + ll + 1, y1 + tl);
		}
		else {
			gotoxy(x1 + tc, y1 + tl);
		}
	}
	call write_tag_entry;
	ret;

unmark_item:
	group_attr = cb_t_color;
	btn_c = button_color;
	bk_c = button_key_color;
	fc = m_s_Color;
	item_marked = FALSE;
	bc = 0;
	call write_istr;
	ret;


parse_item:
	mstr = menu_item_str(menu, c_parm, 3);
	tt = menu_item_int(menu, c_parm, 1);
	var_parse_int("/C=", mstr, tc);
	tl = menu_item_int(menu, c_parm , 3);
	var_parse_int("/QK=", mstr, ll);
	label_str = menu_item_str(menu, c_parm, 1);
	xx = tc + x1;
	yy = tl + y1;
	ret;


	int  tat;
write_istr:
	call parse_item;
write_istr2:
	switch( tt ) {
		case 17:                            // Hidden field
			ret;

		case 20:                            // Directory box
			goto write_dir_window;

		case 11:
			write(label_str, xx + button_down, yy, 0, btn_c);
			if(  ((ll != 0) & (item_marked == 0))  ) {
				Draw_Attr( xx + ll  - 1, yy, button_key_color, 1 );
			}

			LL = svl( Label_Str );
			var_parse_str('/KC=', mstr, label_str );
			Write( label_str, xx + ll + button_down , yy, 0, bk_c );
			tw = svl( label_str );
			if(  SHADOW_BUTTONS   ) {
				if(  button_down == 0  ) {
					Write( '|220', xx + ll + tw, yy, 0, button_shadow_color );
					Draw_Char( 223, xx + 1, yy + 1, button_shadow_color, ll + tw );
				} else {
					Draw_Char( 32, xx /* + ll + tw */, yy, m_t_color, 1 );
					Draw_Char( 32, xx + 1, yy + 1, m_t_color, ll + tw );
				}
			}
			ret;

		case 12 :
			if(  menu_item_int( menu,c_parm, 2 )  ) {
				Write( RADIO_BUTTON_ON , xx, yy, 0, group_attr );
			} else {
				Write( RADIO_BUTTON_OFF , xx, yy, 0, group_attr );
			}
			Write( label_str, xx + CHECK_RADIO_WIDTH, yy, 0, group_attr );
do_draw_qk:
			if(  ll != 0  ) {
				Draw_Attr( xx + ll  + CHECK_RADIO_WIDTH - 1, yy, cb_s_color, 1 );
			}
			ll = 1;
			tw = svl(label_str) + CHECK_RADIO_WIDTH - 1;
			ret;

		case 13 :
			if(  menu_item_int( menu,c_parm, 2 )  ) {
				Write( CHECK_BOX_ON , xx, yy, 0, group_attr );
			} else {
				Write( CHECK_BOX_OFF , xx, yy, 0, group_attr );
			}
			Write( label_str, xx + CHECK_RADIO_WIDTH, yy, 0, group_attr );
			goto do_draw_qk;
		case 15:                            // List box
		case 16:                            // Edit window
case16x:
			menu_count = menu_item_int(menu, c_parm, 2);
			if ((item_marked == FALSE) || (t_parm != c_parm)) {
				rm("di_list_field /M=2/X=" + str(xx) + "/Y=" + str(yy)
						+ "/FC=" + str(fc) + "/Y1=" + str(y1)
						+ "/Y2=" + str(y1 + height - 1)
						+ "/#=" + str(menu_count) + mstr + "/TP=" + str(tt)
						+ "/T=" + menu_item_str(menu, c_parm, 1)
						+ "/NOHI=" + str(item_marked == 0));
			}
			ret;
	} // switch

	VAR_Parse_Int('/ATTR=',Mstr, tat);
	if(  tat == 0  ) {
		tat = m_t_color;
	}
	Write( Label_Str, xx, yy, 0, tat);
	if(  ll != 0  ) {
		Draw_Attr( xx + ll - 1, yy, m_s_color, 1 );
	}
	if(  tt == 10  ) {
		protect = 0;
		tw = 0;
		tmw = 0;
		ret;
	}
	VAR_Parse_Int('/W=',Mstr, TW);
	VAR_Parse_Int('/ML=',Mstr, TMW);
	VAR_Parse_Int( '/PROTECT=', mstr, PROTECT );
	history_stat = (parse_str('/HISTORY=', mstr) != '');
	LL = svl( Label_Str );
	xx = xx + ll;
	if(  (tt == 3) | (TT > 4)  ) {
		++xx;
		draw_char( 91, xx, yy, m_t_color, 1 );
		++xx;
		draw_char( 93, xx + tw, yy, m_t_color, 1 );
	} else {
		++xx;
	}
	switch (tt) {
		case 1 :
			mstr = str(menu_item_int( menu,c_parm, 2 ));
			goto gowrite;
		case 7 :
			mstr = str(menu_item_int( menu,c_parm, 2 ));
			goto gowrite;
		case 9 :
			call blank_fkey_row;
			RM('SETUP^MAKEKEY /K1=' + Str(menu_item_int( menu,c_parm, 2 ) & $FF) + '/K2='
					+ Str((menu_item_int( menu,c_parm, 2 ) >> 8) & $FF));
			MStr = Return_Str;
			goto gowrite;
	}

	mstr = menu_item_str(menu, c_parm, 2);
	switch( tt ) {
	case 0 :
		/* fill_char := 46; */
		break;

	case 5 :
		if(  menu_item_int( menu,c_parm, 2 ) == 0  ) {
			var_parse_str('/F=', mstr, mstr );
		} else {
			var_parse_str('/T=', mstr, mstr );
		}
		break;

	case 4 :  /* Process HEX */
		Mstr = Hex_Str( menu_item_int( menu,c_parm, 2 ) );
		if(  (svl(mstr) % 2) != 0  ) {
			mstr = '0' + mstr;
		}
		break;

	case 3 :  /* Process Menu */
		c_choice = menu_item_int( menu,c_parm, 2 );
		if(  (C_Choice < 1)  ) {
			C_Choice = 1;
		}
		Count = 0;
		jx = 1;
		jz = 1;
menu_loop:
		jx = xpos( '(', mstr, jx + 1 );
		if(  jx != 0  ) {
			if(  copy(mstr, jx + 1, 1) == '('  ) {
				mstr = str_del( mstr, jx, 1);
				goto MENU_loop;
			}
			++count;
			jy = xpos( ')', mstr, jx + 1);
			if(  (Count < C_Choice)  ) {
				jz = jy + 1;
				goto MENU_loop;
			}
		}
		MStr = copy( mstr, jz, jx - jz );
	}

gowrite:
	if(  tw == 0  ) {
		tw = svl(mstr);
	}
	if(  svl(mstr) > TW  ) {
		Write(copy(MStr, 1, tw),xx, Yy,BC,FC);
	} else {
		if(  (tt == 0) & (fc == m_s_color)  ) {
			pad_str(mstr,tw,'|249');
		 /* Draw_char(249, xx + svl(mstr), yy,m_t_color, tw - svl(mstr)); */
		} else {
			pad_str(mstr,tw,' ');
			/* Draw_char(32, xx + svl(mstr), yy,fc, tw - svl(mstr)); */
		}
		Write(mstr, xx, yy, bc, fc );
	}
	if(  (tt == 3) | (tt == 8)  ) {
		Write('|25', xx + tw + 1, yy, 0, button_key_color);
	} else if(  history_stat  ) {
		Write('|222 |221', xx + tw, yy, 0, (button_key_color & $70 >> 4) + (m_b_color & $F0));
		Write('|25', xx + tw + 1, yy, 0, button_key_color);
	}
	ret;


write_fkey_row:
	rm("di_fkey_row /R=" + str(old_fkey_row) + "/TP=" + str(tt)
			+ "/HISTORY=" + str(history_stat) + "/DS=" + str(dialog_style));
	ret;

blank_fkey_row:
	draw_char(32, 1, old_fkey_row, fkey_color, screen_width);
	ret;

find_mouse:
	mouse_result = FALSE;
	jx = 1;
	while (jx <= field_count) {
		mstr = menu_item_str(menu, jx, 3);
		tt = menu_item_int(menu, jx, 1);
		if ((tt != 10) && (tt != 17)) {     // Not Display data or Hidden field
			var_parse_int("/C=", mstr, tc);
			tl = menu_item_int(menu, jx, 3);
			if (tt != 11) {                   // Not Button
				var_parse_int("/W=", mstr, tw);
				if (tw == 0) {
					tw = length(menu_item_str(menu, jx, 2));
				}
			}
			else {
				tw = length(parse_str("/KC=", mstr)) - 2;
			}
			label_str = menu_item_str(menu, jx, 1);
			ll = svl(label_str);
			xx = x1 + tc/*  + ll */;
			if ((tt == 3) || (tt == 8)) {     // Mutiple choice or Run macro (str(
				tw = tw + 3;
			}
			else if ((tt == 12) || (tt == 13)) {  // Radio box or Check box
				tw = check_radio_width - 1;
			}
			else if (tt > 4) {
				++tw;
			}

			if (tt == 20) {                   // Directory box
				if ((mou_last_y >= (dir_y1)) && (mou_last_y <= dir_y2) &&
						(mou_last_x > dir_x1) && (mou_last_x <= dir_x2))
				{
					dir_field_active = parse_int("/DA=", menu_item_str(menu, jx, 2));
					goto mou_field_found;
				}
			}
			else if ((tt == 15) || (tt == 16)) {  // List box or Edit window box
				switch_window(parse_int('/WIN=', mstr));
				if ((mou_last_Y >= win_y1) &&
					(mou_last_y < (win_y2 + (tt == 16))) &&
					(mou_last_x > win_x1) && (mou_last_x <= win_x2))
				{
					goto mou_field_found;
				}
			}
			else if (((tl + y1) == mou_last_y) && (mou_last_x >= xx)) {
				if ((mou_last_x <= (xx + ll + tw))) {

mou_field_found:
					if (c_parm != jx) {
						fjx = jx;
						call unMark_item;
						c_parm = fjx;
						call mark_item;
						call write_fkey_row;
					}
					jx = field_count;
					mouse_result = TRUE;
				}
				else if (parse_str("/HISTORY=", mstr) != "") {
					if ((mou_last_x >= (xx + ll + tw + 1)) &&
							(mou_last_x <= (xx + ll + tw + 3)))
					{
						key1 = 0;
						key2 = 62;
					//  push_key(key1, key2);
						goto mou_field_found;
					}
				}
			}
		}
		++jx;
	}
	ret;

move_box:
	if (parse_int("/NM=", mparm_str)) {
		ret;
	}
	RM('WINDOW^MOVE_WIN /X1=' + str(x1)+'/Y1=' + Str(y1) + '/X2='
			+ Str(x1 + width - 2) + '/Y2=' + Str(y1 + height - 1) + '/MS=' + str(0)
			+ '/MX1=1/MX2=' + Str(Screen_Width)
			+ '/MY1=2/MY2=' + str(screen_length - 1)
			+ '/MM=0/M=2/K=' + str(return_int));
	if(  (virtual_display == 0)  ) {
		kill_box;
	}
	x1 = parse_int('/X1=', return_str);
	y1 =  parse_int('/Y1=', return_str);
	t_parm = 0;
	move = TRUE;
	override_screen_seg;
	call Build_Dialog_Box2;
	reset_screen_seg;
	update_virtual_display;
	reset_virtual_display;
	ret;

build_dialog_box:
	if (x1 == 0) {
		x1 = (screen_width / 2) - ((width - 2) / 2);
	}
	if ((x1 + (width - 2)) > screen_width) {
		x1 = (screen_width - (width - 2));
	}
	if (x1 <= 0) {
		x1 = 1;
	}
	if (y1 == 0) {
		y1 = min_window_row + 1;
	}
	while (((y1 + height - 1) > screen_length) && (y1 > 2)) {
		--y1;
	}
	if ((y1 + height) >= (screen_length)) {
		y1 = screen_length - height - 1;
	}
	if (height == screen_length) {
		y1 = (status_row == 1) + (menu_bar_row != 0) + 1;
	}
	if (y1 <= ((status_row == 1) + (menu_bar_row != 0))) {
		y1 = (status_row == 1) + (menu_bar_row != 0) + 1;
	}

build_dialog_box2:
	refresh = FALSE;
	full_write = FALSE;

	set_virtual_display;
	// If an x coordinate was specified then build a box
	if (box || move) {
		put_box(x1, y1, x1 + width, y1 + height, 0, m_b_color,
						parse_str("/T=", mparm_str), TRUE);
		move = FALSE;
	}
	call draw_all_fields;
	full_write = TRUE;

	update_status_line;
	update_virtual_display;
	reset_virtual_display;
	ret;

go_select_field:
	switch (tt) {
		case 3:
			call input_menu_field;
			break;

		case 5:
			call toggle_field;
			break;

		case 7:
			call run_int_macro;
			break;

		case 8:
			call run_str_macro;
			break;

		case 9:
			call input_quickstroke;
			break;

		case 13:
			call check_box;                   // Check box
			break;

		case 12:                            // Radio box
			if (key1 == 32) {                 // <Space>
				call next_same;
			}
			call radio_box;
			break;

		case 15:                            // List box
			call process_list_field;
	}
	ret;

process_cr:
	 return_int = 0;
		if (dialog_style == 0) {
			if (parse_int("/GO=", menu_item_str(menu, c_parm, 3))) {
				return_int = parse_int("/GO=", menu_item_str(menu, c_parm, 3));
			}
			else {
				call next_item;
				return_int = 0;
			}
		}
		ret;

locate_buttons:
	jx = 1;
	return_int = 0;
	while(  (jx <= field_count)  ) {
		if(  menu_item_int( menu, jx, 1 ) == 11  ) {
			mstr = menu_item_str( menu, jx , 3);
			if(  (key1 == parse_int('/K1=', mstr)) & (key2 == parse_int('/K2=', mstr))  ) {
				return_int = jx;
			}
		}
		++jx;
	}
	mstr = menu_item_str( menu, c_parm , 3);
	ret;


find_quick_key:
	return_int = 0;
	jx = 1;
	while (jx <= field_count) {
		mstr = menu_item_str(menu, jx , 3);
		label_str = menu_item_str(menu, jx, 1);
		Var_Parse_Int("/QK=", mstr, ll);
		if ((ll > 0)) {
			if (caps(str_char(label_str, ll)) == tchar) {
				return_int = jx;
			}
		}
		++jx;
	}
	mstr = menu_item_str(menu, c_parm , 3);
	label_str = menu_item_str(menu, c_parm, 1);
	ll = svl(label_str);
	ret;

locate_quick_keys:
		return_int = 0;
		if ((key2 > 15) && (key2 < 26)) {
			tchar = copy("QWERTYUIOP", key2 - 15, 1);
			call find_quick_key;
		}
		else if ((key2 > 29) && (key2 < 39)) {
			tchar = copy("ASDFGHJKL", key2 - 29, 1);
			call find_quick_key;
		}
		else if ((key2 > 43) && (key2 < 51)) {
			tchar = copy("ZXCVBNM", key2 - 43, 1);
			call find_quick_key;
		}
		else if ((key2 > 119) && (key2 < 131)) {
			tchar = copy("1234567890", key2 - 119, 1);
			call find_quick_key;
			/* right arrow */
		}
		ret;

check_quick_keys:
	call locate_quick_keys;

set_quick_key:
	if(  return_int > 0  ) {
		fjx = return_int;
		call UnMark_Item;
		c_parm = fjx;
		call not_10;
		if(  tt == 12  ) {
			call Radio_Box;
		}
		Call Mark_Item;
		if(  c_parm == fjx  ) {
			return_int = 1;
		} else {
			return_int = 2;
		}
	}
	ret;

quick_keys1:
	tchar = caps(char(key1));
	call find_quick_key;
	call set_quick_key;
	ret;


check_advance:
	if ((key1 == 9) && (key2 == 15)) {    // <TAB>
		call next_item;
	}
	else if ((key1 == 0) && (key2 == 15)) {  // <ShftTAB>
		call previous_item;
	}
	else if ((key1 == 13) && (key2 == 28)) {  // <ENTER>
		call locate_buttons;
		if (return_int) {
			push_key(key1, key2);
			return_int = TRUE;
		}
		else {
			call process_cr;
			if (return_int) {
				goto exit;
			}
		}
	}
	else if ((key1 == 27) && (key2 == 1)) {  // <ESC>
		push_key(key1, key2);
	}
	else if (dialog_style == 0) {
		if (key1 == 0) {
			if ((key2 == 72) || (key2 == 80)) {
				push_key(key1, key2);
			}
		}
	}
	ret;

	int  oi ;

top_of_same:
		while(  (c_parm > 1)  ) {
			--c_parm;
			if(  menu_item_int( menu, c_parm, 1 ) != tt  ) {
				++c_parm;
				ret;
			}
		}
		ret;

clear_same:
		oi = c_parm;
		call top_of_same;
		while(  (c_parm <= field_count)  ) {
			if(  menu_item_int( menu, c_parm, 1 ) == tt   ) {
				if(  menu_item_int( menu, c_parm, 2 ) != 0  ) {
					menu_set_int( menu, c_parm, 2,  0  );
					if(  c_parm != oi  ) {
						call unmark_item;
					}
				}
			} else {
				c_parm = oi;
				ret;
			}
			++c_parm;
		}
		c_parm = oi;
		ret;

bottom_of_same:
		while(  (c_parm < field_count)  ) {
			++c_parm;
			if(  menu_item_int( menu, c_parm, 1 ) != tt  ) {
				--c_parm;
				ret;
			}
		}
		ret;


find_radio_box:
		oi = c_parm;
		tt = 12;
		call top_of_same;
		while(  (c_parm <= field_count)  ) {
			if(  menu_item_int( menu, c_parm, 1 ) == tt  ) {
				if(  menu_item_int( menu, c_parm, 2 )  ) {
					ret;
				}
			} else {
				c_parm = oi;
				ret;
			}
			++c_parm;
		}
		c_parm = oi;
		ret;

next_same:
		oi = c_parm;
		call unmark_item;
		++c_parm;
		if(  menu_item_int( menu, c_parm , 1) != tt  ) {
			c_parm = oi;
			call top_of_same;
		}
		ret;

prev_same:
		oi = c_parm;
		call unmark_item;
		--c_parm;
		if (menu_item_int(menu, c_parm, 1) != tt) {
			c_parm = oi;
			call bottom_of_same;
		}
		ret;


next_non:
		call unmark_item;
		while (c_parm < field_count) {
			++c_parm;
			if (menu_item_int(menu, c_parm , 1) != tt) {
				ret;
			}
		}
		c_parm = 1;
		ret;

prev_non:
	call unmark_item;
	while (c_parm > 1) {
		--c_parm;
		if (menu_item_int(menu, c_parm , 1) != tt) {
			tt = menu_item_int(menu, c_parm, 1);
			call top_of_same;
			ret;
		}
	}
	c_parm = field_count;
	ret;


next_item:
	oi = c_parm;
	call unmark_item;
	if (tt == 13) {                       // Check box
		if (dialog_style || ((key1 == 9) && (key2 == 15))) {  // <TAB>
			call next_non;
			goto ni_2;
		}
	}
	else if (tt == 12) {                  // Radio box
		call next_non;
		goto ni_2;
	}
ni_loop:
	++c_parm;
ni_2:
	call check_c_parm;
	tt = menu_item_int(menu, c_parm , 1);
	if (((tt == 10) || (tt == 17)) && (c_parm != oi)) {  // Display data
		goto ni_loop;
	}
	if (tt == 12) {                       // Radio box
		call find_radio_box;
	}
	ret;


previous_item:
	oi = c_parm;
	call unmark_item;
	if ((tt == 12) || ((tt == 13) && dialog_style)) {
		call prev_non;
		goto pi_2;
	}
pi_loop:
	--c_parm;
pi_2:
	call check_c_parm;
	tt = menu_item_int(menu, c_parm, 1);
	if (((tt == 10) || (tt == 17)) && (c_parm != oi)) {
		goto pi_loop;
	}
	if (tt == 13) {
		if (dialog_style || ((key1 == 0) && (key2 == 15))) {
			call top_of_same;
		}
	}
	else if (tt == 12) {
		call find_radio_box;
	}
	ret;

check_c_parm:
	if (c_parm < 1) {
		c_parm = field_count;
	}
	else if (c_parm > field_count) {
		c_parm = 1;
	}
	ret;

not_10:
	oi = c_parm;
	tt = menu_item_int(menu, c_parm , 1);
	if (tt == 10) {
		while (TRUE) {
			if (((tt == 10) || (tt == 17)) && (c_parm < field_count)) {
				++c_parm;
				tt = menu_item_int(menu, c_parm , 1);
				continue;
			}
			if (tt == 12) {
				call find_radio_box;
			}
			break;
		}
	}
	ret;

process_edit_field:
	rm("di_edit_window " + menu_item_str(menu, c_parm, 3));
	ret;


process_list_field:
	rm("di_list_field /#=" + str(menu_count) + "/TP=" + str(tt)
			+ menu_item_str(menu, c_parm, 3)
			+ "/T=" + menu_item_str(menu, c_parm, 1));
	if (parse_int("/REDRAW1=", menu_item_str(menu, c_parm, 3))) {
		oi = c_parm;
		c_parm = 1;
		call unmark_item;
		c_parm = oi;
		call parse_item;
	}
	call write_tag_entry;
	ret;

/*
adjust_list:
	RM('DI_LIST_FIELD /M=1/#=' + str(menu_count));
	ret;
*/

calculate_menu_count:
	mark_pos;
	eof;
	menu_count = c_line;
	if(  c_col == 1  ) {
		--menu_count;
	}
	goto_mark;
	ret;

	/*  returns:
			ll := width,
			menu_count := count,
			hc := height
		*/
calculate_list_field:
	mark_pos;
	tof;
	menu_count = 0;
	ll = 0;
	if( !parse_int("/W=", mstr) ) {
		while(  NOT( at_eof )  ) {
			++menu_count;
			jx = length(get_line);
			if(  jx > ll  ) {
				ll = jx;
			}
			down;
		}
	} else {
		if( !parse_int("/HT=", mstr) )
			call calculate_menu_count;
	}
	goto_mark;
	if(  at_eof  ) {
		goto_col(1);
		if(  at_eof  ) {
			goto_line( c_line - 1 );
		}
	}

	++ll;
	if(  (xx + ll) > (screen_width - 5)  ) {
		ll = screen_width - 5 - xx;
	}
	hc = menu_count;
	hl = 5 + (status_row != 0) + (old_fkey_row != 0)
			+ (menu_bar_row != 0) + Display_Lines;
	if(  (jy + hc) > (screen_length - hl)  ) {
		hc = (screen_length - jy - hl) - 1;
	}
	ret;


input_string_history:
	if (!protect) {
		check_key;
		push_key(0, 80);                    // push <DN> key

input_string_field:
		if (protect) {
			check_key;
		}
		else {
			return_str = menu_item_str(menu, c_parm, 2);
			call string_in;
			if (return_int) {
				menu_set_str(menu, c_parm, 2, return_str);
				// file tagging enabled and on Name field
				if (tag_sd && (c_parm == dir_msk_field)) {
					if ((key1 == 0) && (key2 == 57)) {  // <AltSPACE>
						c_parm = dir_field_found;
						call parse_item;
						call process_dir_field;
						t_parm = 0;
						c_parm = dir_msk_field;
						call mark_item;
					}
				}
				call check_advance;
			}
		}
		return_int = 1;
	}
	ret;


input_integer_field:
	if(  not( protect )  ) {
		Return_Str = Str(menu_item_int( menu, c_parm, 2 ));

		call string_in;
		if(  return_int  ) {
			if(  Val( jx, Return_Str) != 0  ) {
				error_level = 1006;
				call meerror;
				ret;
			}
			Mstr = menu_item_str( menu, c_parm , 3);
			if(  xpos( '/MIN=', MStr,1) != 0  ) {
				jy = parse_int('/MIN=', MStr );
				if(  jx < jy  ) {
					error_level = 1006;
					call meerror;
					ret;
				}
			}
			if(  xpos( '/MAX=', MStr,1) != 0  ) {
				jy = parse_int('/MAX=', MStr );
				if(  jx > jy  ) {
					error_level = 1006;
					call meerror;
					ret;
				}
			}
			menu_set_int( menu, c_parm, 2, jx );
			call Check_Advance;
		} else {
			return_int = 1;
		}
	}
	ret;


meerror:
	fkey_row = old_fkey_row;
	RM('MEERROR');
	error_level = 0;
	return_int = 0;
	ret;

input_hex_field:
	if(  not( protect )  ) {
		/* Process hex input */
		Return_Str = Hex_Str(menu_item_int( menu, c_parm, 2 ));
		call string_in;
		if(  return_int  ) {
			if(  val(jx, '$' + caps(return_str)) != 0  ) {
				RM('MEERROR^Beeps /C=1');
				return_int = 0;
			} else {
				menu_set_int( menu, c_parm, 2, jx );
				call Check_Advance;
			}
		}
	}
	ret;

input_real_field:
	if(  not( protect )  ) {
/* Process real input */
		Return_Str = menu_item_str( menu, C_PARM, 2);
		call string_in;
		if(  return_int  ) {
			if(  Rval(T_Real,return_str) != 0  ) {
				RM('MEERROR^Beeps /C=1');
				return_int = 0;
			} else {
				menu_set_str( menu, c_parm, 2, return_str );
				call Check_Advance;
			}
		}
	}
	ret;

prepare_macro:
	Return_Str = Copy(menu_item_str( menu,  C_Parm, 3  ),Return_Int + 3,
		255);
	if(  (XPos(' ',Return_Str,1) == 0)  ) {
		Return_Str = Return_Str + ' ';
	}
	return_str = '/X=' + Str(X1) + '/Y=' + Str(Y1) + '/PRE=' + prefix;
	ret;

run_int_macro:
	Return_Int = XPos('/M=',menu_item_str( menu,  C_Parm, 3  ),1);
	if(  (Return_Int)  ) {
		call prepare_macro;
		return_int = menu_item_int( menu, c_parm, 2 );
		call blank_fkey_row;
		fkey_row = old_fkey_row;
		call set_fields;
		RM(Return_Str + '/INT=' + Str(menu_item_int( menu, c_parm, 2 )));
		call reset_fields;
		menu_set_int( menu, c_parm, 2, Return_Int );
		return_int = 1;
		call draw_all_Fields;
	}
	ret;

run_str_macro:
	Return_Int = XPos('/M=',menu_item_str( menu,  C_Parm, 3  ),1);
	if(  (Return_Int)  ) {
/* Check to see if command line parameters already exist in the macro name.  If
so, don't add a space between macro name and the params we are adding */
		MStr = Copy(menu_item_str( menu,  C_Parm, 3  ),Return_Int + 3,
			255);
		if(  (XPos(' ',MStr,1) == 0)  ) {
			MStr = MStr + ' ';
		}
		Return_Str = menu_item_str( menu, C_PARM, 2);
		if(  (RGS != '')  ) {
			Set_Global_Str(RGS,menu_item_str(menu, c_parm, 2));
		}
		fkey_row = old_fkey_row;
		call blank_fkey_row;
		call set_fields;
		RM(MStr + '/X=' + Str(X1) + '/Y=' + Str(Y1)
				+ '/STR=' + menu_item_str( menu, C_PARM, 2) + '/PRE=' + prefix);
		call reset_fields;
		if(  return_int > 0  ) {
			menu_set_str( menu, c_parm, 2, return_str );
			if(  (RGS != '')  ) {
				menu_set_str( menu, c_parm, 2, global_str( rgs ));
			}
		}
		call draw_all_Fields;
		return_int = 1;
	}
	ret;

Input_QuickStroke:
	Put_Box(X1,Y1,X1 + 25,Y1 + 3,0,M_B_Color,'EDIT KEY ASSIGNMENT',true);
	Write('Press the desired key.',X1 + 1,Y1 + 1,0,M_B_Color);
	Read_Key;
	Kill_Box;
	menu_set_int( menu, c_parm, 2, (key2 * 256) + key1);
	Ret;


Input_Menu_Field:
	rm('USERIN^XMENU /T=1/B=1/X=' + Str(X1 + TC + LL + 1) + '/Y=' + Str(Yy + 1)
			+ '/L=' + Shorten_Str(Label_Str) + '/S=' + Str(C_Choice)
			+ '/M=' + menu_item_str( menu, C_PARM, 2));

	if(  Return_Int > 0  ) {
		menu_set_int( menu, c_parm, 2,  Return_Int );
	}
	Ret;

check_box:
toggle_field:
	menu_set_int(menu, c_parm, 2, !menu_item_int(menu, c_parm, 2));
	ret;


radio_box:
	call clear_same;
	menu_set_int(menu, c_parm, 2,  1);
	mstr = menu_item_str(menu, c_parm, 3);
	var_parse_str("/G=", mstr, c_str1);
	if (svl(c_str1) > 0) {
		set_global_int(c_str1, parse_int("/R=", mstr));
	}
	ret;

string_in:
	while (TRUE) {
		if (tmw == 0) {
			tmw = tw;
		}
		mstr = menu_item_str(menu, c_parm, 3);
		call set_fields;
		rm("di_string_in /HISTORY=" + parse_str("/HISTORY=", mstr)
				+ "/X=" + str(x1 + tc + ll + 1) + "/Y=" + str(yy) + "/W=" + str(tw)
				+ "/L=" + str(tmw) + "/SPCH=" + parse_str("/SPCH=", mstr)
				+ "/HM=" + parse_str("/HM=", mstr) + "/PRE=" + prefix
				+ "/TAG=" + str(tag_sd && (c_parm == dir_msk_field))
				+ "/HID=" + parse_str("/HID=", mstr));  // Mod for hidden field

		call reset_fields;
		if (return_int < 0) {          // -1 = mouse outside string
			if (return_int == -2) {           // -2 = unprocessed key
				call locate_quick_keys;
				if (!return_int || (return_int == c_parm)) {
					call locate_buttons;
					if (return_int) {

            push_key(key1, key2); // Fix by T.R. 1-6-94
            return_int = TRUE;

						break;
					}
					continue;
				}
			}
			push_key(key1, key2);
			return_int = TRUE;
		}
		break;
	}
	if (history_stat) {
		mstr = menu_item_str(menu, c_parm , 3);
		if (parse_int("/NHA=", mstr) == 0) {
			jx = return_int;
			rm("history_list /M=1/HISTORY=" + parse_str("/HISTORY=", mstr));
			return_int = jx;
		}
	}
	if (parse_int("/HM=", mstr)) {
		oi = return_int;
		call draw_all_fields;
		return_int = oi;
	}
	ret;

write_dir_window:
	if ((item_marked == FALSE) || (t_parm != c_parm)) {
		return_int = 1 + (item_marked == 0);
		call dir_field_macro;
		if (((return_int & 0x01) && (item_marked == TRUE))
				|| (dir_sd && (dir_sd < dir_field_found)))
		{

			int oldx = wherex,
					oldy = wherey;

			c_parm = dir_sd;
			call unmark_item;
			gotoxy(oldx, oldy);
		}
		c_parm = dir_msk_field;
		call unmark_item;
		c_parm = dir_field_found;
		call parse_item;
	}
	ret;

process_dir_field:
	return_int = 3;
	call dir_field_macro;
	if (return_int) {

		int oldx = wherex,
				oldy = wherey;

		if (return_int & 0x01) {
			c_parm = dir_sd;
			call unmark_item;
		}
		if (return_int & 0x02) {            // Refresh TAG list window
			c_parm = tag_sd;
			call unmark_item;
		}
		gotoxy(oldx, oldy);
	}
	c_parm = dir_msk_field;
	call unmark_item;
	c_parm = dir_field_found;
	call parse_item;
	ret;


dir_field_macro:
	tc = return_int;
	return_int = menu;
	rm("di_dir_field /M=" + str(tc) + "/F#=" + str(c_parm)
				+ "/X=" + str(x1) + "/Y=" + str(y1));
	ret;


write_tag_entry:
	if (tag_update && (tt == 15) && (c_parm == tag_sd)) {

		int t_width = 48;
		str tstr = str_del(get_line, 1, 13);

		if (svl(tstr) > t_width) {
			tstr = copy(tstr, 1, t_width);
		}
		pad_str(tstr, t_width, " ");
		pad_str(tstr, 60, " ");
		write(tstr, x1 + 1, dir_y2 + 1, 0, m_t_color);
	}
	ret;

int  tx ;

set_fields:
	if (!use_handle) {
		tx = 0;
		while (tx < field_count) {
			++tx;
			set_global_str(istr + str(tx), menu_item_str(menu, tx, 2));
			set_global_int(iint + str(tx), menu_item_int(menu, tx, 2));
		}
	}
	ret;

reset_fields:
	if (!use_handle) {
		tx = 0;
		while (tx < field_count) {
			++tx;
			menu_set_str(menu, tx, 2, global_str(istr + str(tx)));
			menu_set_int(menu, tx, 2, global_int(iint + str(tx)));
		}
	}
	ret;


exit:
	set_global_int('!DATA_IN_#', c_parm );

	if ((return_int == 0) && (parse_int('/VA=', mparm_str) == TRUE)) {
/* Verify abort */
		rm("verify /S=1/H=" + parse_str("/H=", mparm_str)
				+ "/T=Are you sure you want to cancel these changes?");
		if (return_int == 0) {
			goto main_loop;
		}
		else {
			return_int = 0;
		}
	}

	if (box && !parse_int("/NK=", mparm_str)) {
		kill_box;
	}
	call blank_fkey_row;
	if(  (cur_window != old_win) & (parse_int('/NOW=', mparm_str) == 0)  ) {
		switch_window( old_win );
	}
	GotoXY(Old_c,Old_l);

	if (dir_field_found) {
		jx = return_int;
		return_int = 4;
		call dir_field_macro;
		return_int = jx;
	}
	mode = orig_mode;
/*
	if(  mode == TERM  ) {
		old_fkey_row = 0;
	}
*/
	fkey_row = old_fkey_row;

	if(  (Fkey_Set)  ) {
		Set_Global_Int('!DI_FKEY_ROW',0);
	}

	set_global_int('!DATA_IN_X', x1 );
	set_global_int('!DATA_IN_Y', y1 );
	var_parse_str('/POSG=', mparm_str, mstr );
	if(  mstr != ''  ) {
		set_global_str( mstr, '/X=' + str(x1) + '/Y=' + str(y1) );
	}

	Tc = 0;
	while(  (Tc < field_count)  ) {
		++Tc;
		Mstr = menu_item_str( menu, TC , 3);
		tt = menu_item_int( menu, tc , 1);
		if(  (tt < 2)  ) {
			if(  (tt == 0)  ) {
/* Convert |xx back to non printable characters on string type only if required */
				if(  (Parse_Int('/SPCH=',Mstr))  ) {
					return_str = _ValChar(menu_item_str( menu, tc, 2));
// Double all ASCII 127 characters so they can be parsed out of a DB file.
					jx = 1;
					while (jx = xpos("",return_str,jx))
						return_str = str_ins("",return_str,++jx++);
					menu_set_str( menu, tc, 2, return_str );
				}
				if (parse_str("/G=", mstr) != "") {
/* If they wish to use a specified storage global, store value in that
10-03-90 TR */
					set_global_str(parse_str("/G=", mstr), menu_item_str(menu, tc, 2));
				}
			}
			else if (tt == 1) {
				if (parse_str("/G=", mstr) != "") {
// If they wish to use a specified storage global, store value in that
// 10-03-90 TR
					set_global_Int(parse_str("/G=", mstr), menu_item_int(menu, tc, 2));
				}
			}
		}
	}

	if (!use_handle) {
		call set_fields;
		tc = 0;
		while (tc < field_count) {
			++tc;
			set_global_str(istr + str(tc), menu_item_str(menu, tc, 2));
			set_global_int(iint + str(tc), menu_item_int(menu, tc, 2));
		}
		// free up as much garbage as possible
		if (parse_int("/NC=", mparm_str) == 0) {
			tc = 0;
			while (Tc < field_count) {
				++tc;
				set_global_str(iparm + str(tc), "");
			}
		}
		menu_delete(menu);
	}
	dir_search_attr = t_dsa;
	set_global_int('MENU_LEVEL', Global_Int('MENU_LEVEL') - 1);
	pop_labels;
	refresh = t_refresh;
}

macro di_string_in trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:  DI_STRING_IN

Description:  Called by DATA_IN to do string input.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int L_Offset = 1,
			str_in_pos = 0,
			first_time,
			jx,

			dialog_style = GLOBAL_INT('@DIALOG_STYLE'),
			xx = parse_int('/X=', mparm_str),
			yy = parse_int('/Y=', mparm_str),
			tw = parse_int('/W=', mparm_str),
			tmw = parse_int('/L=', mparm_str),
			specchar = Parse_Int('/SPCH=',mparm_str),
			tag = parse_int("/TAG=", mparm_str),
			hid = parse_int('/HID=', mparm_str)         			// ldh 09-Oct-92
			;

	str hid_str;                                          // ldh 09-Oct-92

string_input:
	hid_str = "";                                         // ldh 09-Oct-92
	for (jx = tmw; jx; --jx) {                            // ldh 09-Oct-92
		hid_str = hid_str + "*";                            // ldh 09-Oct-92
	}                                                     // ldh 09-Oct-92
	first_time = TRUE;
	l_offset = 0;
	str_in_pos = 1;

string_input_loop:
	return_int = FALSE;
	call redraw_string_input;
	read_key;
	if(  ((key1 == 13) & (key2 == 224))  ) {
		Key2 = 28;
	}
	if (key1 == 0) {
		if (key2 == 3) {
/* This allows insertion of a null character */
			Goto INSERT_CTRL_CHAR;
		}
		if(  key2 == 77  ) {
			++str_in_pos;
		}
		else if(  key2 == 82  ) {
			insert_mode = NOT(insert_mode);
		}
		else if(  key2 == 75  ) {
			--str_in_pos;
		}
		else if(  key2 == 79  ) {
			str_in_pos = length(return_str) + 1;
		}
		else if(  key2 == 71  ) {
			str_in_pos = 1;
			L_Offset = 0;
		}
		else if(  key2 == 15  ) {
			return_int = TRUE;
			goto string_input_exit;
		}
		else if(  key2 == 116  ) {
			jx = length( return_str );
			while(  (str_in_pos <= jx) &
						(XPOS( copy(return_str, str_in_pos, 1), word_delimits, 1 ) ==0)  ) {
				++str_in_pos;
			}
			while(  (str_in_pos <= jx) &
						(XPOS( copy(return_str, str_in_pos, 1), word_delimits, 1 ) != 0)  ) {
				++str_in_pos;
			}
		}
		else if(  key2 == 115  ) {
			if(  str_in_pos > 1  ) {
				--str_in_pos;
				while(  (str_in_pos > 1) &
							(XPOS( copy(return_str, str_in_pos, 1), word_delimits, 1 ) != 0)  ) {
					--str_in_pos;
				}
				while(  (str_in_pos > 1) &
							(XPOS( copy(return_str, str_in_pos - 1, 1), word_delimits, 1 ) ==0)  ) {
					--str_in_pos;
				}
			}
		}
		else if(  (key2 == 80)  ) {
			if(  (dialog_style)  ) {
				goto do_history_list;
			}
			else {
				return_int = TRUE;
				goto string_input_exit;
			}
		}
		else if(  (key2 == 72)  ) {
			if(  NOT(dialog_style)  ) {
				return_int = TRUE;
				goto string_input_exit;
			}
		}
		else if(  (key2 == 62)  ) {

do_history_list:
			if(  parse_str('/HISTORY=', mparm_str ) != ''  ) {
				Set_Global_Str( '!TEMP_RETURN_STR!', return_str);
				if(  parse_int('/HM=', mparm_str)  ) {
					RM( parse_str('/HISTORY=', mparm_str) + ' /X=' + str(xx)
							+ '/Y=' + str(yy + 1) + '/PRE=' + Parse_Str('/PRE=',MParm_Str));
				}
				else {
					RM('HISTORY_LIST /M=0/X=' + str(xx) + '/Y=' + str(yy + 1)
							+ '/HISTORY=' + parse_str('/HISTORY=', mparm_str ) );
				}
				if(  NOT( return_int )  ) {
					return_str = Global_Str('!TEMP_RETURN_STR!');
				}
				set_global_str('!TEMP_RETURN_STR!', '');
				goto string_input_loop;
			}
		}
		else if (key2 == 83) {
			return_str = str_del( return_str, str_in_pos, 1 );
		}
		else if (key2 == 250) {             // <MEVENT>
			if ((mou_last_y == yy) && (mou_last_x >= xx) &&
					(mou_last_x < (xx + tw)))
			{
				str_in_pos = (l_offset + (mou_last_x - xx)) + 1;
			}
			else {
				return_int = -1;
				goto exit;
			}
		}
		else if ((key2 == 57) && tag){      // <AltSPACE> for file tagging
			return_int = 2;
			goto exit;
		}
		else {
			return_int = -2;
			goto exit;
		}
	}
	else if(  ((key1 == 9) & (key2 == 15)) | ((key1 == 13) & (key2 == 28))
				| ((key1 == 27) & (key2 == 1))  ) {
	 /* IF dialog_style THEN
			return_int := -2;
		ELSE */
			return_int = TRUE;
	 /* END; */
		goto string_input_exit;
	} else if(  (key1 == 8) & (key2 == 14)  ) {
		if(  (str_in_pos > 1)  ) {
			--str_in_pos;
			return_str = str_del( return_str, str_in_pos, 1 );
		} else if(  first_time  ) {
			return_str = '';
		}
	} else {
INSERT_CTRL_CHAR:
		if(  first_time  ) {
			return_str = '';
		}
		if(  NOT(insert_mode)  ) {
			return_str = str_del( return_str, str_in_pos, 1 );
		}
		if(  ((SPecchar != 0) & (Xpos(Char(Key1),'|26|0|9|255|13|27',1) > 0))  ) {
/* This handles non printable ASCII characters */
			return_str = str_ins('||' +  Str( key1 ), return_str, str_in_pos );
			Str_In_Pos = Str_In_Pos + Length(Str(Key1));
		} else {
			return_str = str_ins( char( key1 ), return_str, str_in_pos );
	}
		++str_in_pos;

	}
	first_time = FALSE;
	goto string_input_loop;

string_input_exit:
	goto exit;


Redraw_String_Input:
	if(  Length( return_str ) > tmw  ) {
		return_str = copy( return_str, 1, tmw );
	}
	if(  str_in_pos < 1  ) {
		str_in_pos = 1;
	} else if(  str_in_pos > tmw  ) {
		str_in_pos = tmw;
	}
	if(  str_in_pos > (length(return_str) + 1)  ) {
		str_in_pos = length(return_str) + 1;
	}
	if(  (str_in_pos - L_Offset ) > tw  ) {
		L_Offset = str_in_pos - tw;
	} else if(  (str_in_pos <= L_Offset)  ) {
		L_Offset = str_in_pos - 1;
	}
	jx = length( return_str ) - l_offset;
	if(  jx > tw  ) {
		jx = tw;
	}
	if (hid)                                                            // ldh 09-Oct-92
		write(copy(hid_str, L_Offset + 1, tw), xx, yy, 0, m_h_color);     // ldh 09-Oct-92
	else                                                                // ldh 09-Oct-92
		write(copy(return_str, L_Offset + 1, tw), xx, yy, 0, m_h_color);  // ldh 09-Oct-92
	draw_char( 176, xx + jx, yy, m_h_color, tw - jx );
	gotoxy( xx + (str_in_pos - L_Offset) - 1, yy );
	ret;

exit:
}  // di_string_in

macro di_list_field trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:  DI_LIST_FIELD

Description:  Called by DATA_IN to process a list field.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str display_macro[50] = parse_str("/DISPMAC=", mparm_str);

	int menu_count = parse_int("/#=", mparm_str),
			jx,
			fc,
			hc,
			ht,
			tt,
			xx,
			yy,
			ht,
			tw,
			mrk = parse_int("/MRK=", mparm_str),
			inc_offs = 1 + mrk,
			m = parse_int("/M=", mparm_str ),
			display_lines = parse_int("/DISP#=", mparm_str),
			nh = parse_int("/NOHI=", mparm_str ),
			ncwa = parse_int("/NCWA=",mparm_str) // special parameter for calc
			;

	jx = parse_int("/INCO=", mparm_str );
  if(jx < 0)
  {
    inc_offs = -1;
  }
  else if (jx != 0) {
		inc_offs = 1 + jx;
	}
	refresh = FALSE;
	if (display_macro != "") {
		if (xpos(" ", display_macro, 1) == 0) {
			display_macro = display_macro + " ";
		}
	}
	return_int = 0;
	switch (m) {
		case 0:
			goto process_list_field;
			break;

		case 1:
			call adjust_list;
			break;

		case 2:
			call write_field;
			break;
	}
	goto exit;

goto_inc_offs:
  if(inc_offs < 0)
  {
    goto_col(1);
    forward_till_not( " \xFF\t\x10\xAF" );
  }
  else
  {
    goto_col(inc_offs);
  }
  ret;


process_list_field:
	mode = EDIT;
	refresh = TRUE;
	call lo_light_list;
	if (key1 == 0) {                      // Extended keys
    call goto_inc_offs;
		if (key2 == 80) {                   // <DN>
			if (c_line < menu_count) {
				down;
				call skip_down;
				call skip_up;
			}
		}
		else if (key2 == 72) {              // <UP>
			up;
			call skip_up;
			call skip_down;
		}
		else if (key2 == 81) {              // <PGDN>
			refresh = FALSE;
			mark_pos;
			page_down;
			if (at_eof) {
				goto_mark;
				goto_col(1);
				while (!at_eof) {
					down;
				}
				up;
        call goto_inc_offs;
			}
			else {
				pop_mark;
			}
			call skip_down;
			call skip_up;
			refresh = TRUE;
			redraw;
		}
		else if (key2 == 71) {              // <HOME>
			tof;
			call skip_down;
			call skip_up;
		}
		else if (key2 == 73) {              // <PGUP>
			page_up;
			call skip_up;
			call skip_down;
		}
		else if (key2 == 79) {              // <END>
			refresh = FALSE;
			mark_pos;
			page_down;
			if (at_eof) {
				goto_mark;
				goto_col(1);
				while (!at_eof) {
					down;
				}
				up;
        call goto_inc_offs;
			}
			else {
				pop_mark;
				eof;
        call goto_inc_offs;
			}
			call skip_up;
			call skip_down;
			refresh = TRUE;
			redraw;
		}
		else if (key2 == 250) {             // <MEVENT>
			if(  (Mou_Last_Y > Win_Y1) & (mou_Last_Y < Win_Y2)  ) {
				if(  (mou_last_x == win_x2)  ) {
					RM('MOUSE^HandleScrollBar /EOF=1/M=1/HI=1/L=' + str(menu_count));
          call goto_inc_offs;
					if(  AT_EOF  ) {
						up;
					}

/* Adjust for illegal line if neccessary */
					call lo_light_list;
					if (return_int == 1) {
						call SKIP_DOWN;
						call SKIP_UP;
					}
					else {
						call SKIP_UP;
						call SKIP_DOWN;
					}

				}
				else if(  (mou_last_x < win_x2) & (mou_last_x > win_x1)  ) {
					jx = c_line;
					RM('MOUSE^MouseInWindow');
					RM('MOUSE^Mouse_Move /V=1/NEOF=1');
					if(  c_line > menu_count  ) {
						while(  c_line > jx  ) {
							UP;
						}
					}
					if (xpos('|254',get_line,1)) {
/* If we landed on an illegal line, move back to where we were */
						if (c_line > jx) {
							while(  c_line > jx  ) {
								UP;
							}
						}
						else {
							while(  c_line < jx  ) {
								DOWN;
							}
						}
						goto SKIP_DC;
					}
					call hi_light_list;

					if (parse_int("/DC=", mparm_str)) {
						if (mou_double_click) {
							push_key(13, 28);
						}
					}
skip_dc:
          call goto_inc_offs;
				}
			}
		}
		else if (((key2 == 223) || (key2 == 251)) && (mrk)) {
			RM('MOUSE^MouseInWindow');
			call skip_up;
			call skip_down;
			goto do_mark;
		}
		else {
			return_int = -1;
		}
    call goto_inc_offs;
	}
	else if ((key1 == 32) && mrk) {       // <SPACE>

do_mark:
		goto_col(1);
		jx = insert_mode;
		insert_mode = FALSE;
		if (cur_char == '|175') {
			text(" ");
		}
		else {
			text('|175');
		}
		insert_mode = jx;
    call goto_inc_offs;
	}
	else if (((key1 > 31) && (key2 < 127)) || (key1 == 8)) {
		refresh = FALSE;
		mark_pos;
		jx = 0;
    int tc = c_col;
    call goto_inc_offs;
    return_str = copy(get_line, c_col, tc - c_col);
		if(  key1 == 8  ) {
			return_str = str_del(return_str, length(return_str), 1);
		}
		else {
			return_str = return_str + char(key1);
		}
		return_str = caps(return_str);
		tof;
    call goto_inc_offs;
incremental_loop:
    if(  caps(copy( get_line, c_col, length(return_str))) == return_str  ) {
			jx = c_line;
		}
		else {
			down;
      call goto_inc_offs;
			if(  NOT(at_eof)  ) {
				goto incremental_loop;
			}
		}
		goto_mark;
		if(  jx != 0  ) {
			while(  (jx > c_line)  ) {
				DOWN;
			}
			while(  (jx < c_line)  ) {
				up;
			}
      call goto_inc_offs;
      goto_col( length(return_str) + c_col);
			redraw;
		}
		refresh = TRUE;
	} else {
		return_int = -1;
	}
	call hi_light_list;
	refresh = FALSE;
	goto exit;

write_none_available:
	if ((win_x2 - win_x1) > 15) {
		write("(none available)", win_x1 + 1, wherey, 0, l_color);
	}
	ret;

lo_light_list:
	goto_col( c_col );
	if (c_line > menu_count) {
		call write_none_available;
	}
	else {
		if (ncwa == 0) {
			draw_char(179, win_x1, wherey, b_color, 1);
		}
	// draw_attr( win_x1 + 1, wherey, t_color, win_x2 - win_x1 - 1 );
	}
	ret;

hi_light_list:
    int tc = c_col;
    call goto_inc_offs;
    if (tc > c_col) {
      goto_col(tc);
    }
    goto_col(c_col);
    if (c_line > menu_count) {
      call write_none_available;
    }
    else {
      if (svl(display_macro) && !nh) {
        str t_disp_str;
        rm(display_macro + "/S=" + str(c_line));
        jx = 0;
        while (jx < display_lines) {
          ++Jx;
  // only draw something if there is something to draw!
          t_disp_str = parse_str("/DS" + str(jx) + "=", return_str);
          if (svl(t_disp_str)) {
            draw_char(32,win_x1,win_y2 + jx, d_t_color, win_x2 - win_x1 + 1);
            write(t_disp_str, win_x1, win_y2 + jx, 0, d_t_color);
          }
        }
      }
      if (ncwa == 0) {
        draw_char(16, win_x1, wherey, l_color, 1);
      }
    }
  //  draw_attr( win_x1 + 1, wherey, h_color, win_x2 - win_x1 - 1 );
    ret;

skip_down:
	while ((xpos('|254', get_line, 1) != 0) && (c_line < menu_count)) {
		down;
	}
	call adjust_list;
	ret;

skip_up:
	while ((xpos('|254', get_line, 1) != 0) && (c_line > 1)) {
		Up;
	}
	call adjust_list;
	ret;


write_field:
	refresh = FALSE;
	var_parse_int('/TP=', mparm_str, tt);
	var_parse_int('/FC=', mparm_str, fc);
	var_parse_int('/X=', mparm_str, xx);
	var_parse_int('/Y=', mparm_str, yy);
	var_parse_int('/HT=', mparm_str, ht );
	set_virtual_display;
	mode = EDIT;
	switch_window( parse_int('/WIN=', mparm_str ) );
	size_window( xx, yy, xx + parse_int('/W=', mparm_str), yy + HT );
	if (ncwa == 0) {
		t_color = cb_t_color;
		c_color = cb_t_color;
		s_color = cb_s_color;
		h_color = m_h_color;
		if( fc == m_h_color ) {
			l_color = m_h_color;
			b_color = cb_s_color;
		} else {
			b_color = cb_t_color;
			l_color = cb_s_color;
		}
		lb_color = m_h_color;
	}
	if(  tt == 15  ) {
		if (ncwa == 0) {
			if(  ht > menu_count  ) {
				window_attr = $1E;
				hc = win_y1 + 1;
				while(  hc < win_y2  ) {
					draw_char( 179, win_x2, hc, b_color, 1 );
					++hc;
				}
			} else {
				window_attr = $16;
			}
			eof_color = (cb_t_color & $F0) | ((cb_t_color & $70) >> 4);
		}
	} else {
		eof_color = cb_s_color;
		l_color = cb_s_color;
		lb_color = cb_h_color;
		window_attr = $06;
	}
	if (ncwa == 0) {
		hc = win_y1 + 1;
		while(  hc < win_y2  ) {
			draw_char( 179, win_x1, hc, b_color, 1 );
			++hc;
		}
	}
	jx = parse_int('/QK=', mparm_str);
	if ( (((t_color & $F0) == (m_b_color & $F0)) || parse_int('/OL=', mparm_str))
				&& (ncwa == 0))
	{
		if(  (win_y1 > parse_int('/Y1=', mparm_str))  ) {
			draw_char( 196, win_x1 + 1, win_y1, b_color, win_x2 - win_x1 - 1 );
			draw_char( 218, win_x1, win_y1, b_color, 1 );
			draw_char( 191, win_x2, win_y1, b_color, 1 );
		}
		if(  (win_y2 < parse_int('/Y2=', mparm_str))  ) {
			draw_char( 196, win_x1 + 1, win_y2, b_color, win_x2 - win_x1 - 1 );
			draw_char( 217, win_x2, win_y2, b_color, 1 );
			draw_char( 192, win_x1, win_y2, b_color, 1 );
		}
		write( parse_str('/T=', mparm_str), xx + 1, yy, 0, b_color );
		if(  jx != 0  ) {
			Draw_Attr( xx + jx, yy, s_color, 1 );
		}
	} else {
		write( parse_str('/T=', mparm_str), xx, yy, 0, m_t_color );
		if(  jx != 0  ) {
			Draw_Attr( xx + jx - 1, yy, m_s_color, 1 );
		}
	}
	if(  tt == 16  ) {
		draw_char( 192, win_x1, win_y2, b_color, 1 );
		draw_char( 217, win_x2, win_y2, b_color, 1 );
	}
	var_parse_int('/#=', mparm_str, menu_count );
	if(  tt == 15  ) {
		call adjust_list;
	}
	refresh = TRUE;
	update_window;
	if(  tt == 15  ) {
		call hi_light_list;
	}
	refresh = false;
	update_virtual_display;
	reset_virtual_display;
  tc = c_col;
  call goto_inc_offs;
  if (tc > c_col) {
    goto_col(tc);
	}
	ret;

adjust_list:
	hc = win_y2 - win_y1;
	if ((menu_count - c_line) < (hc - c_row - 1)) {
		refresh = FALSE;
		jx = c_line;
		hc = jx - hc;
		if (hc < 1) {
			hc = 1;
		}
		while (c_line > hc) {
			up;
		}
		while (c_line < menu_count) {
			down;
		}
		while (c_line > jx) {
			up;
		}
		refresh = TRUE;
		update_window;
	}
	ret;

exit:
	refresh = FALSE;
}  // di_list_field

macro DI_FKEY_ROW TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:  DI_FKEY_ROW

Description:  Writes the fkey row for the current field.


							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  old_fkey_row = parse_int('/R=', mparm_str ),
					 tt = parse_int('/TP=', mparm_str ),
					 dialog_style = parse_int('/DS=', mparm_str )
					 ;
	str  tstr[80] ;

	fkey_row = 0;
	tstr = 'Next field' + global_str('!NEXT_KEY') + ', Previous' + GLOBAL_STR('!PREV_KEY');

	if(  tt == 0  ) {
		RETURN_STR = 'STRING INPUT:';
	} else if(  tt == 1  ) {
		RETURN_STR = 'NUMERIC INPUT:';
	} else if(  tt == 2  ) {
		RETURN_STR = 'FLOATING POINT:';
	} else if(  tt == 3  ) {
		RETURN_STR = 'MULTIPLE CHOICE:';
		tstr = tstr + ', Choices<Spacebar> or <|25>';
	} else if(  tt == 4  ) {
		RETURN_STR = 'HEX INPUT:';
	} else if(  tt == 5  ) {
		RETURN_STR = 'TOGGLE:';
		tstr = tstr + ', Toggle<Spacebar>';
	} else if(  (tt == 9)  ) {
		return_str = 'KEY ASSIGNMENT:';
		tstr = tstr + ', Select<Spacebar> Delete<Del>';
	} else if(  (tt >= 7) & (tt < 11)  ) {
		RETURN_STR = 'SELECT:';
		tstr = tstr + ', Select<Spacebar>';
	} else if(  (tt == 11)  ) {
		RETURN_STR = 'BUTTON:';
		tstr = tstr + ', Select<Spacebar>';
	} else if(  (tt == 12)  ) {
		RETURN_STR = 'RADIO BUTTON:';
		tstr = tstr + ', Next selection<Spacebar> ' + GLOBAL_STR('!GROUP_KEY');
	} else if(  (tt == 13)  ) {
		RETURN_STR = 'CHECK BOX:';
		tstr = tstr + ', Toggle<Spacebar>, ' + GLOBAL_STR('!GROUP_KEY') + ' to select';
	} else if(  (tt == 15)  ) {
		RETURN_STR = 'LIST BOX:';
		tstr = 'Next field<TAB>,Previous<ShftTAB>, |24|25<PgUp><PgDn><Home><End>';
	} else if(  (tt == 16)  ) {
		RETURN_STR = 'EDIT BOX:';
		tstr = '|24|25|26|27<PgUp><PgDn><Home><End>';
	} else if(  (tt == 20)  ) {
		RETURN_STR = 'DIR LIST:';
		tstr = 'Next field<TAB> Previous<ShftTAB>';
		tstr = tstr + ', Parent dir<CtrlBS>, Select<ENTER>';
	}
	if(  (tt <= 3) | (tt ==4)  ) {
		if(  parse_str('/HISTORY=', mparm_str) != ''  ) {
			if(  dialog_style == 0  ) {
				tstr = tstr + ', History list<F4>';
			} else {
				tstr = tstr + ', History list<|25>';
			}
		}
	}
	write( return_str + '  ', 1, old_fkey_row, 0, fnum_color );
	write( tstr, 2 + length(return_str), old_fkey_row, 0, fkey_color );
	draw_char( 32, svl(tstr) + 2 + length(return_str), old_fkey_row, fkey_color, screen_width - svl(tstr) );

} /* DI_FKEY_ROW */

macro di_dir_field trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:  di_dir_field

Description:  Directory field handler to be called by data_in.

Parameters:
					 /M=nn    operation number
								0   Initialize field
								1   Draw field highlighted
								2   Draw field un-highlighted
								3   Process field.
								4   Close field

					 /F#=nn   Field number
					 /X=nn    X coordinate of box
					 /Y=nn    Y coordinate of box

Returns:
		 RETURN_INT = -1 (for /M=3) process keystroke.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int m,
			x,
			y,
			c_parm,
			xx,
			yy,
			dir_field_active,
			item_marked,
			dd_row,
			oi,
			width,
			orig_mode = mode,
			sdd,
			res = 0,
			menu,
			ow = cur_window,
			tag_win,
			tag,
			ntm,
			jx,
			tag_num=0,	// 102293[scm]
			msk
			;

	str prefix[10],
			last_dir_entry[80],
			tstr[80],
			tag_mac
			;

	var_parse_int("/M=", mparm_str, m);
	var_parse_int("/X=", mparm_str, x);
	var_parse_int("/Y=", mparm_str, y);
	var_parse_int("/F#=", mparm_str, c_parm);
	menu = return_int;
	return_str = menu_item_str(menu, c_parm, 2);
	dd_row = parse_int("/DR=", return_str);
	last_dir_entry = parse_str("/LE=", return_str);
	dir_field_active = parse_int("/DA=", return_str);
	dir_search_attr = 0x27;

	sdd = parse_int("/SDD=", menu_item_str(menu, c_parm, 3));
	tag = parse_int("/TAG=", menu_item_str(menu, c_parm, 3));  // TAG support
	if (tag) {
		tag_win = parse_int("/WIN=", menu_item_str(menu, tag, 3));
		ntm = parse_int("/NTM=", menu_item_str(menu, tag, 3));
		tag_mac = parse_str("/TAGMAC=", menu_item_str(menu, tag, 3));
		if (tag_mac != "") {
			if (!xpos(" ", tag_mac, 1)) {
				tag_mac += " ";
			}
		}
	}
	msk = parse_int("/MSK=", menu_item_str(menu, c_parm, 3));
	if (msk == 0) {
		msk = c_parm - 1;
	}

	switch (m) {
		case 0:
			call init_field;
			break;

		case 1:
			call hi_field;
			call set_istr;
			break;

		case 2:
			call lo_field;
			call set_istr;
			break;

		case 3:
			call process_dir_field;
			call set_istr;
			break;

		case 4:
			call close_field;
			break;

		case 5:
			call select_dir_entry;
			break;
	}
	goto exit;

init_field:
	set_global_str("!OLD_DIR_COLORS", "/D_T=" + str(d_t_color)
									+ "/D_S=" + str(d_s_color) + "/D_H=" + str(d_h_color)
									+ "/D_B=" + str(d_b_color));
	d_t_color = cb_t_color;
	d_s_color = cb_s_color;
	d_h_color = m_h_color;
	d_b_color = cb_s_color;

	rm("dirShell^initDirShell /SMALL=1/S=2");
	set_global_int("@DIR_CUR_DIR@", 5);
	open_dir(5);
	switch_dir(5);
	dir_sort_str = global_str("DIR_SORT_STR");
	dir_mode = 1;
	ret;

set_istr:
	menu_set_str(menu, c_parm, 2, "/DR=" + str(dd_row) + "/LE=" + last_dir_entry
																+ "/DA=" + str(dir_field_active));
	ret;

hi_field:
	item_marked = TRUE;
	call draw_field;
	ret;

lo_field:
	item_marked = FALSE;
	call draw_field;
	ret;

draw_field:
	tstr = menu_item_str(menu, c_parm, 3);
	xx = x + parse_int("/C=", tstr);
	yy = y + menu_item_int(menu, c_parm, 3);

	mode = DOS_SHELL;
	size_dir(xx, yy, xx + 30, yy + 14, 0);
	refresh = TRUE;
	if (dir_field_active || item_marked || dir_immediate) {
		return_int = dd_row;
		if (item_marked) {
			if ((menu_item_str(menu, msk, 2) != last_dir_entry)
					|| !dir_field_active)
			{
				call set_new_dir_mask;
			}
			call update_dir;
			call set_dir_entry;
		}
		else {
			if (dir_immediate && !dir_field_active) {
				call set_new_dir_mask;
				call update_dir;
			}
			rm("dirShell^updateDir /M=2/ND=1");
			call write_dir_entry;
		}
		dd_row = return_int;
	}
	else {
		oi = d_b_color;
		d_b_color = d_t_color;
		redraw_dir;
		d_b_color = oi;
		write("Directory List", xx + 8, yy, 0, button_color );
		write("Select this field to get", xx + 1, yy + 1, 0, d_t_color );
		write("a directory listing.", xx + 1, yy + 2, 0, d_t_color );
		gotoxy(xx + 8, yy);
	}
	refresh = FALSE;
	mode = orig_mode;
	ret;

set_file_name:
	menu_set_str(menu, msk, 2, return_str);
	ret;

set_new_dir_mask:
	return_str = caps(menu_item_str(menu, msk, 2));
	if (return_str == "?NO-FILE?") {
		return_str = "";
	}
	if (get_path(return_str) == "") {
		return_str = get_path(dir_mask) + return_str;
	}
	return_str = fexpand(return_str);
	if (!xpos("?", return_str, 1) && !xpos("*", return_str, 1)) {
		if (copy(return_str, length(return_str), 1) != "\\") {
			if ((file_attr(return_str) & 0x10) != 0) {
				return_str = return_str + "\\";
			}
		}
		if (get_extension(return_str) != "") {
			return_str = get_path(return_str) + "*." + get_extension( return_str );
		}
		else {
			if ((m == 1) || (m == 3))
				if (!xpos("?", return_str, 1) && !xpos("*", return_str, 1)) {
					// return_str = get_path(return_str);
					tstr = truncate_path(return_str);
					while (svl(tstr) > 0) {
						push_key(ascii(str_char(tstr, svl(tstr))), 0);
						tstr = str_del(tstr, svl(tstr), 1);
					}
					return_str = truncate_extension(return_str) + "*.*";
				}
		}
	}

set_new_dir_mask2:
	if ((return_str != dir_mask) || !dir_field_active) {
			rm("dirShell^processDir");
			if (error_level != 0) {
				rm("meError");
				error_level = 0;
			}
			return_str = dir_mask;
			last_dir_entry = return_str;
			call create_dir_list;
	}
	ret;


create_dir_list:
	if (sdd) {

		int jx,
				tr = refresh,
				oldx = wherex,
				oldy = wherey,
				dir_win = parse_int("/WIN=", menu_item_str(menu, sdd, 3));

		refresh = FALSE;
		working;
		switch_window(dir_win);
		erase_window;
		if (length(get_path(fexpand(dir_mask))) > 3) {
			put_line("..\\");
			down;
		}
		file_search_attr = 0x37;
		jx = first_file(get_path(dir_mask) + "*.*");
		while (jx == 0) {
			if (last_file_attr & 0x10) {
				if ((last_file_name != ".") && (last_file_name != "..")) {
					put_line(last_file_name + "\\");
					down;
				}
			}
			jx = next_file;
		}
		jx = c_line;
		qsort_lines(1, c_line - 1, 1, 1, 255, 0);
		goto_line(c_line);
		tstr = parse_str("/DS=", global_str("@DIR_PARMS@"));
		jx = 0;
		while (jx++ < svl(tstr)) {
			put_line(str_char(tstr, jx) + ":");
			down;
		}
		menu_set_int(menu, sdd, 2, c_line - 1);
		tof;
		call mark_tags;
		refresh = tr;
		res |= 0x01;
		gotoxy(oldx, oldy);
	}
	ret;

update_dir:
	rm("dirShell^updateDir /M=1/ND=1");
	dd_row = return_int;
	return_str = dir_mask;
	dir_field_active = TRUE;
	call write_dir_entry;
	ret;

dir_field_input:
	mode = DOS_SHELL;
	refresh = TRUE;
	if (!dir_field_active) {
		call set_new_dir_mask;
		call set_file_name;
		call update_dir;
	}
	else {
		call select_dir_entry;
		if (return_str != last_dir_entry) {
			last_dir_entry = return_str;
			call set_file_name;
		}
	}
	refresh = FALSE;
	mode = orig_mode;
	ret;


write_dir_entry:
	if (!global_int("@TAG_LIST_WIN")) {
		width = 48;
		tstr = dir_entry_str;
		if (svl(tstr) > width) {
			tstr = copy(tstr, 1, width);
		}
		pad_str(tstr, width, " ");
		write(tstr, x + 1, dir_y2 + 1, 0, m_t_color);
	}
	ret;

process_dir_field:
	if (tag && ((key1 == 0) && (key2 == 57))) {  // <AltSPACE>
		call add_tag;
		ret;
	}

	int mb2 = FALSE;

	if (!dir_field_active) {
		rm("meError^Beeps /C=1");
		call dir_field_input;
		ret;
	}
	if (tag) {
		if (key1 == 32) {                   // <SPACE>
			call add_tag;
			key1 = 0;
			key2 = 77;
		}
		else if ((key1 == 0) && (key2 == 83)) {  // <DEL>
			call del_tag;
			if (global_int("@TAG_LIST_WIN")) {
				call lo_field;
			}
			else {
				call hi_field;
			}
			ret;
		}
		else if (key2 == 251) {             // <MEVENT2>
			mb2 = TRUE;                       // TMJ
		}
	}
	mode = DOS_SHELL;
	refresh = TRUE;
	tstr = dir_mask;
	working;
	rm("dir_shell_event /DM=1/ND=1");
	redraw;
	if (return_int > 0) {
		call select_dir_entry;
		if (return_int) {
			push_key(13, 28);                 // <ENTER>
		}
	}
	else {
		if (tstr != dir_mask) {
			call create_dir_list;
		}
		call set_dir_entry;
		if (mb2) {
			call add_tag;
		}
		if ((key1 == 0) && (key2 == 77)) {
			rm("dirShell^updateDir /M=1/ND=1");
		}
		return_int = 0;
	}
	call write_dir_entry;
	mode = orig_mode;
	refresh = FALSE;
	ret;


set_dir_entry:
	if (dir_num > 0) {
		return_str = dir_entry;
		if ((dos_file_attr & 0x10) != 0) {
			return_str = return_str + "\\";
		}
		last_dir_entry = return_str;
		call set_file_name;
	}
	ret;

select_dir_entry:
	if ((dos_file_attr & 0x10) != 0) {
		return_str = fexpand(dir_entry);
		rm("dirShell^processDir");
		if (error_level != 0) {
			rm("meError");
			error_level = 0;
		}
		rm("dirShell^updateDir /M=1/ND=1");
		call write_dir_entry;
		return_str = dir_mask;
		call create_dir_list;
		return_int = 0;
	}
	else {
		return_str = dir_entry;
		call set_file_name;
		return_int = 1;
	}
	ret;


add_tag:
	switch_window(tag_win);

	int oldx = wherex,
			oldy = wherey,
			srf = refresh,
			line = c_line;

	return_str = caps(menu_item_str(menu, msk, 2));
	refresh = FALSE;
	tof;
	tstr = truncate_path(return_str);
	pad_str(tstr, 13, " ");
	if (get_path(return_str) == "") {
		return_str = get_path(dir_mask) + shorten_str(tstr);
	}
	return_int = (dir_entry == return_str);
	tstr += return_str;
	if (tag_mac != "") {
		rm(tag_mac);
		tstr += return_str;
	}
	if (!find_text(tstr, 0, 0)) {
		eof;
		if (c_col != 1) {
			cr;
		}
		text(tstr);
		return_str = " Tagged.";
		if (return_int && !file_marked) {
			refresh = TRUE;
			mark_file;
			refresh = FALSE;
		}
	}
	else {
		return_str = " Untagged.";
		del_line;
		if (return_int && file_marked) {
			refresh = TRUE;
			mark_file;
			refresh = FALSE;
		}
	}
	if (!ntm) {
		make_message(shorten_str(copy(tstr, 1, 12)) + return_str);
	}
	eof;
	if (c_col != 1) {
		down;
	}
	menu_set_int(menu, tag, 2, c_line - 1);
	goto_line(line);
	goto_col(1);
	res |= 0x02;
	gotoxy(oldx, oldy);
	refresh = srf;
	ret;


del_tag:
	switch_window(tag_win);

	int oldx = wherex,
			oldy = wherey,
			line = c_line;

	if ((tstr = get_line) != "") {
		if (!ntm) {
			make_message(get_word(" ") + " Untagged.");
		}
		del_line;
		call unmark_tag;
	}
	eof;
	if (c_col != 1) {
		down;
	}
	menu_set_int(menu, tag, 2, c_line - 1);
	goto_line(line);
	goto_col(1);
	res |= 0x02;
	gotoxy(oldx, oldy);
	ret;


mark_tags:
	if ( tag ) // 102293[scm]
	{

	if ((tag_num = menu_item_int(menu, tag, 2)) > 0) {

		int cnt;

		switch_window(tag_win);
		line = c_line;
		tof;
		for (cnt = 0; cnt < tag_num; ++cnt) {
			tstr = get_line;
			tstr = copy(tstr, 14, svl(tstr) - 13);
			if (jx = xpos(" ", tstr, 1)) {
				tstr = copy(tstr, 1, jx - 1);
			}
			if (get_path(tstr) == get_path(dir_mask)) {
				dos_home;
				if (dir_inc_search(truncate_path(tstr))) {
					if (!file_marked) {
						mark_file;
					}
				}
			}
			down;
		}
		dos_home;
		goto_line(line);
		goto_col(1);
	}
	} // 102293[scm]
	ret;


unmark_tag:
	tstr = copy(tstr, 14, svl(tstr) - 13);
	if (jx = xpos(" ", tstr, 1)) {
		tstr = copy(tstr, 1, jx - 1);
	}
	if (get_path(tstr) == get_path(dir_entry)) {
		jx = dir_num;
		dos_home;
		if (dir_inc_search(truncate_path(tstr))) {
			if (file_marked) {
				mark_file;
			}
		}
		dos_home;
		while (jx != dir_num) {
			dos_right;
		}
	}
	ret;


close_field:
	close_dir(5);
	tstr = global_str('!OLD_DIR_COLORS');
	d_t_color = parse_int('/D_T=', tstr );
	d_s_color = parse_int('/D_S=', tstr );
	d_H_color = parse_int('/D_H=', tstr );
	d_B_color = parse_int('/D_B=', tstr );
	ret;

exit:
	switch_window(ow);
	refresh = FALSE;
	return_int = res;

}  // di_dir_field

macro DI_EDIT_WINDOW TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:  DI_EDIT_WINDOW

Description:  Called only by DATA_IN maro.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str
			allowed_ext_keys[40] = '|82|83|72|80|77|75|71|79|116|115|119|117|73|81|132|118|15',
			ts[1],
			tstr[20]
					;
	int t, pb = persistent_blocks, tundo = undo_stat, wn = window_id;
	undo_stat = TRUE;
	persistent_blocks = Parse_Int('/PB=',MParm_Str);
	refresh = TRUE;
	mode = edit;
	lb_color = h_color;
	goto_col(c_col);
LOOP:
	READ_KEY;
skip_read_key:
	refresh = true;
	t = left_offset;

		// Keys which cannot be overridden
	switch(  key1 )
	{
		case 27 :
			goto do_key_switch;

		case 0 :
			switch( key2 )
			{
				case 250 :
					goto do_key_switch;
			}
	}

	IF (Inq_Key( Key1, Key2, 128, TStr )) {
		pass_key(key1,key2);
		if(window_id != wn)
			switch_win_id( wn );
		goto loop;
	}

	do_key_switch:
	switch(  key1 ) {
		case 0 :
			switch( key2 ) {
			case 250 :
				if(  (mou_last_x >= win_x1) & (mou_last_x <= win_x2) &
					(mou_last_y >= win_y1) & (mou_last_y <= win_y2)  ) {
					RM( 'MOUSE^MouEvent /M=2/S=1' );
				} else {
					return_int = 10;
					goto EXIT;
				}
				break;

		//	case :
		// 	RM('DELEOL');
		//		break;

			case 146 :
				rm('MEUTIL2^CUT');
				break;

			case 147 :
				rm( 'DELWORD' );
				break;

			case 14 :
				undo;
				break;

			case 15 :
				if( read_only ) {
					return_int = 10;
					goto EXIT;
				}
				rm("TAB /M=1");
				refresh = TRUE;
				redraw;
				break;

			default:
				if(  xpos( CHAR(key2), allowed_ext_keys, 1) != 0  ) {
					Pass_Key( key1, key2 );
				} else {
					RETURN_INT = 10;
					goto exit;
				}
			}
			break;
		case 253 :
			switch( key2 ) {
				case 147:
					rm('DELEOL');
					break;
				case 119 :
				case 141 :
				case 132 :
				case 115 :
				case 116 :
				case 117 :
				case 145 :
				case 118 :
					rm("shift_cursor");
			}
			break;
		case 48:
			if( key2 == 82) {
				rm('MEUTIL2^PASTE');
				redraw;
			}
			else
				goto pass1;
			refresh = TRUE;
			redraw;
			break;

		case 46:
			if( key2 == 83)
				rm('MEUTIL2^CUT /M=1');
			else
				goto pass1;
			refresh = TRUE;
			redraw;
			break;

		case 127 :
			if( key2 == 14)
				rm('BSWORD');
			else
				goto pass1;
			break;

		case 27 :
			return_int = 10;
			goto EXIT;
		case 9 :
			if( read_only ) {
				return_int = 10;
				goto EXIT;
			}
			rm("TAB");
			refresh = TRUE;
			redraw;
			break;
		case 8 :
			back_space;
			break;
		case 13 :
			rm("CR");
			break;

		default :
			if( key2 > 70 ) {
				if(( key1 > 47) && (key1 < 58)) {
					rm("shift_cursor");
					goto loop;
				}
			}
			pass1:
			if( !inq_key( key1, key2, 0, ts ) )
				Pass_Key( key1, key2 );
			else
				text( char(key1) );
	}
	GOTO LOOP;

CHECK_EDIT_MAC:
/*  IF Inq_Key( Key1, Key2, 10, TStr ) THEN

	END;  */
	RET;

EXIT:
	undo_stat = tundo;
	refresh = false;
	persistent_blocks = pb;
} /* DI_EDIT_WINDOW */

macro HISTORY_LIST TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: HISTORY_LIST

Description:  Adds to or lists a history list.

Parameters:  /M=0      List history
						 /M=1      Add to history
						 /X=n      X coordinate for history list
						 /Y=n      Y coordinate for history list
						 /HISTORY=str  History list name
						 /H=str    Help string

Global_Int("@HISTORY_LENGTH") = amount of entries allowed in a history list.
																default = 15

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  m = parse_int('/M=', mparm_str),
					x = parse_int('/X=', mparm_str),
					y = parse_int('/Y=', mparm_str),
					s,
					c,
					jx, temp_integer
				 ;
	str  history_str[20] = parse_str('/HISTORY=', mparm_str ) ;

	if (global_int("@HISTORY_LENGTH") < 1) {
		set_Global_int("@HISTORY_LENGTH",15);
	}

	if(  m == 0  ) {
	list_history:
		c =  parse_int( '/#=' , global_str(history_str));
		s = 1;
		if(  global_str(history_str + '0') != ''  ) {
			set_global_str( history_str + str(c + 1), global_str(history_str + '0') );
			++c;
			++s;
		}
		RM('GlobalVarList /REV=1/G=' + history_str + '/X=' + str(x)
				+ '/Y=' + str(y) + '/T=HISTORY/H=' + parse_str('/H=', mparm_str)
				+ '/#=' + str( c ) + '/SC=' + str(s));
		if(  s > 1  ) {
			set_global_str( history_str + str(c + 1), '' );
		}
	} else if(  m == 1  ) {
	add_to_history:
		if(  (History_Str == 'FILE_HISTORY')  ) {
			return_str = caps(return_str);
		}
		if(  return_str != ''  ) {
			jx = parse_int('/#=', global_str(history_str));
			temp_integer = 0;
			while(  temp_integer < jx  ) {
				++temp_integer;
				if(  global_str( history_str + str(temp_integer) ) == return_str  ) {
					return_int = temp_integer;
					RM('deleteitem /G=' + history_str  + '/#=' + str(jx));
					--jx;
					temp_integer = jx;
				}
			}
			if (jx > Global_Int("@HISTORY_LENGTH")) {
				set_global_str( history_str + '0', '' );
				return_int = 1;
				RM('deleteitem /G=' + history_str  + '/#=' + str(jx));
			} else {
				++jx;
			}
			set_global_str( history_str + str(jx), return_str);
			set_global_str( history_str, '/#=' + str(jx));
		}
	}
}
