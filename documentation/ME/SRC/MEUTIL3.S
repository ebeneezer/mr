macro_file MEUTIL3;
/*******************************************************************************
													MULTI-EDIT MACRO FILE MEUTIL3

ABOUTME					- Displays an "ABOUT" dilog box.
WINDMENU				- Brings up the window menu.
USER						- Brings up the user menu.
SORTMENU				- The text sorting dialog box.
LOADMACRO				- Prompts for a macro file to load.
LISTMACS				- Creates a list of macros.
LISTGLOB				- Creates a list of global variables.
LAYOUT					- The layout dialog box.
PRINTSTR				- Sends a string to the printer.
PRINT_FILE_BLOCK- Sends a file or marked block to the printer.
OPEN_CLOSE_FILE	- Opens or closes a file or device.
PRINT						- The Printing dialog box.
PRN_TYPE        - Brings up the printer type selection menu.
PRN_SET_DEVICE  - Brings up the printer device selection menu.
PRN_CHECK_DEVICE- Makes sure the printer device is open.
PRN_EJECT_PAGE	-	Sends a form feed character to the printer.
PRN_CODES				- Brings up a menu of printer codes and allows user to send them
									directly to the printer
PRINT_CODE_MENU	- The printer code selection menu.
NOTINC					- Presents an "Option not included" message.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro AboutMe TRANS {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: ABOUTME

Description: Displays an 'ABOUT' dialog box

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  menu = menu_create ;
	menu_set_item( menu, 1, 'ÚÄÄÄÄÄÄ·                      ÚÄÄÄÄ·             ','',
			'/L=1/C=4/ATTR=' + str( m_s_color ),10,0, 0);
	menu_set_item( menu, 2, '³ É¸É¸ º    A Text Editor     ³ÉÍÍÍ¼             ','',
			'/L=2/C=4/ATTR=' + str( m_s_color ),10,0, 0);
	menu_set_item( menu, 3, '³ º³º³ ºÚ· ÚÄ·Ú·  ÚÄÄÄÄ·Ú·    ³ÓÄÄ· ÚÄÄÄ·Ú·ÚÄÄÄÄ·','',
			'/L=3/C=4/ATTR=' + str( m_s_color ),10,0, 0);
	menu_set_item( menu, 4, '³ º³º³ º³º ³ º³º  ÔÍ¸ÉÍ¼³ºÚÄÄ·³ÉÍÍ¼ ³ É¸º³ºÔÍ¸ÉÍ¼','',
			'/L=4/C=4/ATTR=' + str( m_s_color ),10,0, 0);
	menu_set_item( menu, 5, '³ º³º³ º³ÓÄÙ º³ÓÄÄ· ³º  ³ºÔÍÍ¼³ÓÄÄÄ·³ ÓÙº³º  ³º  ','',
			'/L=5/C=4/ATTR=' + str( m_s_color ),10,0, 0);
	menu_set_item( menu, 6, 'ÔÍ¼Ô¼ÔÍ¼ÔÍÍÍÍ¼ÔÍÍÍ¼ Ô¼  Ô¼    ÔÍÍÍÍ¼ÔÍÍÍ¼Ô¼  À½  ','',
			'/L=6/C=4/ATTR=' + str( m_s_color ),10,0, 0);
	menu_set_item( menu, 7, 'Version ' + version,'',	'/L=7/C=20',10,0, 0);
  menu_set_item( menu, 8, 'Copyright(c) 1988, 1993 by American Cybernetics, Inc.','',
			'/L=9/C=2',10,0, 0);

	menu_set_item( menu, 9, 'Multi-Edit is protected under United States copyright ','',
			'/L=11/C=2',10,0, 0);
	menu_set_item( menu, 10, 'law.  Copies may be  made for  the purpose  of backup','',
			'/L=12/C=2',10,0, 0);
	menu_set_item( menu, 11, 'ONLY.  Unless licensed under the terms of a  separate','',
			'/L=13/C=2',10,0, 0);
	menu_set_item( menu, 12, 'agreement which is signed  by an authorized agent  of','',
			'/L=14/C=2',10,0, 0);
	menu_set_item( menu, 13, 'American Cybernetics, Inc., this software is licensed','',
			'/L=15/C=2',10,0, 0);
	menu_set_item( menu, 14, 'for use by only one person at a time and on only  one','',
			'/L=16/C=2',10,0, 0);
	menu_set_item( menu, 15, 'computer at a time.','',
			'/L=17/C=2',10,0, 0);
	return_int = menu;
	RM('UserIn^Data_In /HN=1/H=*/A=1/#=15/T=');
	menu_delete( menu );
}

macro windmenu trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: WINDMENU

Description: Brings up the window menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	rm('MENU /MN=WINDOW' + mparm_str);
}

macro user trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: USER

Description: Brings up the user menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	rm('MENU /MN=MECONFIG.DB^USER');
}

macro SORTMENU TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: SORTMENU

Description:  The text sorting dialog box.

Global Variables:
							Global_Str('SORTPARMS') stores the following:
								/C= The starting column of sort key
								/L= Number of character in sort key
								/B The sort marked block flag
								/CASE Ignore case flag
								/D Descending order flag


							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str  gstr[80], ustr[4] ;
	int  sort_col, sort_length, sort_mode, sort_case, sort_order ;
	int  jx, menu = menu_create;

	gstr = Global_Str('SORTPARMS');
	sort_col = Parse_Int('/C=', gstr);
	sort_length = Parse_Int('/L=', gstr);
	sort_mode = (xpos('/B', gstr, 1) != 0);
	sort_case = (xpos('/CASE',gstr,1) != 0);
	sort_order = (xpos('/D', gstr, 1) == 0);

	menu_set_item( menu, 1, 'Starting column of sort key.................','',
			'/QK=1/C=1/W=4/L=1',1,sort_col, 0);
	menu_set_item( menu, 2, 'Number of characters in sort key (0 = all)..','',
			'/QK=1/C=1/W=4',1,sort_length, 0);
	menu_set_item( menu, 3, 'Options:','', '/C=1/L=4',10,0, 0);
	menu_set_item( menu, 4, 'Sort Marked Block Only ','', '/QK=13/C=2',13,sort_mode, 0);
	menu_set_item( menu, 5, 'Ignore Case            ','', '/QK=1/C=2',13,sort_case, 0);
	menu_set_item( menu, 6, 'Sort order:','', '/C=1/L=7',10,0, 0);
	menu_set_item( menu, 7, 'Ascending  ','', '/QK=1/C=2',12,sort_order, 0);
	menu_set_item( menu, 8, 'Descending ','', '/QK=1/C=2',12,(sort_order == 0), 0);
	return_int = menu;
	RM('UserIn^Data_In /HN=1/H=SORTTEXT/S=2/#=8/T=TEXT SORT');
	sort_order = menu_item_int( menu, 7, 2 );
	sort_case = menu_item_int( menu, 5, 2 );
	sort_mode = menu_item_int( menu, 4, 2 );
	sort_length = menu_item_int( menu, 2, 2 );
	sort_col = menu_item_int( menu, 1, 2 );
	gstr = '/C=' + str(sort_col) + '/L=' + str(sort_length);
	if(  (sort_case)  ) {
		gstr = gstr + '/CASE';
	}
	if(  sort_mode  ) {
		gstr = gstr + '/B';
	}
	if(  sort_order == 0  ) {
		gstr = gstr + '/D';
	}
	set_global_str('SORTPARMS', gstr);
	if(  return_int  ) {
		jx = parse_int('/BC=', mparm_str);
		while(  box_count > jx  ) {
			kill_box;
		}
		RM('TEXT^TEXTSORT ' + gstr);
		return_int = 1;
	}
	menu_delete( menu );
}

macro LOADMACRO TRANS {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: LOADMACRO

Description:  Prompts for a macro to load.

Parameters:
							/BC= Number of boxes to kill upon exit

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
		int jx;

		set_global_str('!MAC_ISTR_1', '');
		set_global_str('!MAC_IPARM_1', '/HISTORY=MACRO_HISTORY/C=1/W=40/T=Name:/ML=80');
		rm('USERIN^Data_In /A=2/T=LOAD MACRO FILE/#=1/PRE=!MAC_/H=MC');
		return_str = global_str('!MAC_ISTR_1');
		Set_Global_Str('!MAC_ISTR_1', '');
		if(  (Return_Int)  ) {
			Load_Macro_File(Return_Str);
			if(  error_level != 0  ) {
				RM('MEERROR');
				return_int = 0;
			} else {
				jx = parse_int('/BC=', mparm_str);
				while(  box_count > jx  ) {
					kill_box;
				}
				make_message('Macro file "' + return_str + '" loaded.');
				return_int = 100;
			}
		}
}

macro LISTMACS TRANS {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: LISTMACS

Description:  Builds a list of macros currently in memory.


							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str TStr[20];
	int Active_window,T_Refresh;

	working;
	Active_Window = Window_Id;
	T_REfresh = Refresh;
	Refresh = False;
	Create_Window;
	if(  (Error_Level)  ) {
		RM('MEERROR');
		Goto EXIT;
	}
	TStr = First_Macro;
Loop:
	if(  TStr != ''  ) {
		Put_Line(TStr);
		Down;
		TStr = Next_Macro;
		Goto Loop;
	}

	tof;
	Set_Global_Str('MEIPARM_1', '/TP=15/C=1/L=0/WIN=' + str(cur_window) );
	RM('USERIN^DATA_IN /A=2/T=LOADED MACROS/A=1/H=LISTMACS/#=1/PRE=ME/CBT=Done');

	Delete_WIndow;
	Switch_Win_Id(Active_WIndow);

EXIT:

	Refresh = T_Refresh;
	Return_Int = 0;

}

macro LISTGLOB TRANS {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: LISTGLOB

Description:  Builds a list of globals currently in memory.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str  astr[20], TStr[40] ;
	int  attr, twindow, tr, tbc ;
	tbc = box_count;
	tr = refresh;
	refresh = false;
	working;
	twindow = window_id;
	switch_window( window_count );
	create_window;
	aStr =  First_Global( attr );
Loop:
	if(  aStr != ''  ) {
		tstr = astr + copy('                     ', 1, 21 - svl(astr) );
		if(  attr == 0  ) {
			tstr = tstr + 'STRING' ;
		} else {
			tstr = tstr + 'INTEGER' ;
		}
		if(  key1 == 27  ) {
			goto exit;
		}
		put_line( tstr );
		down;
		aStr = Next_Global( attr );
		Goto Loop;
	}
	tof;
again:
	call clear_boxs;

	Set_Global_Str('GEIPARM_1', '/DC=1/TP=15/T=Select global to view//change:/W=31/L=1/C=1/WIN=' + str(cur_window));
	RM('USERIN^DATA_IN /A=2/T=GLOBAL VARIABLES/H=MC/#=1/PRE=GE/ABT=View/CBT=Done');
	if(  (Return_Int == 1)  ) {
		tstr = shorten_str( copy(Get_Line,1,20));
		if(  copy( Get_Line, 22, 1) == 'S'  ) {
			set_global_str('!GLOB_ISTR_1', tstr);
			set_global_str('!GLOB_IPARM_1', '/PROTECT=1/C=1/W=20/T=Name: ');
			set_global_str('!GLOB_ISTR_2', global_str(tstr));
			set_global_str('!GLOB_IPARM_2', '/C=1/W=60/T=Value:/ML=2048');
			rm('USERIN^Data_In /A=2/T=EXAMINING GLOBAL STRING/S=2/#=2/PRE=!GLOB_');
			if(  (Return_Int)  ) {
				set_global_str( tstr, global_str('!GLOB_ISTR_2' ));
			}
			Set_Global_Str('!GLOB_ISTR_1', '');
			Set_Global_Str('!GLOB_ISTR_2', '');
		} else {
			set_global_str('!GLOB_ISTR_1', tstr);
			set_global_str('!GLOB_IPARM_1', '/PROTECT=1/C=1/W=20/T=Name: ');
			set_global_int('!GLOB_IINT_2', global_int(tstr));
			set_global_str('!GLOB_IPARM_2', '/TP=1/C=1/W=9/T=Value:');
			rm('USERIN^Data_In /A=2/T=EXAMINING GLOBAL INTEGER/S=2/#=2/PRE=!GLOB_');
			if(  (Return_Int)  ) {
				set_global_int( tstr, global_int('!GLOB_IINT_2' ));
			}
			Set_Global_Str('!GLOB_ISTR_1', '');
			Set_Global_Int('!GLOB_IINT_2', 0);
		}
		Goto again;
	}
	goto exit;

clear_boxs:
	while(  box_count > tbc  ) {
		kill_box;
	}
	ret;

exit:
	call clear_boxs;
	refresh = false;
	delete_window;
	switch_win_id( twindow );
	Return_Int = 0;
	refresh = tr;
}

macro LAYOUT trans2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: LAYOUT

Description:  The layout dialog box

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int menu = menu_create,
			jx
			;

	return_int = menu;
	rm('RESOURCE^_DG_LAYOUT');
	jx = xpos( '|16', format_line, 1 );
	if(jx > 0)
			--jx;
	menu_set_int( menu, 2, 2, doc_mode == 0 );
	menu_set_int( menu, 3, 2, doc_mode != 0 );
	menu_set_int( menu, 5, 2, indent_style == 0 );
	menu_set_int( menu, 6, 2, indent_style == 1 );
	menu_set_int( menu, 7, 2, indent_style == 2 );
	menu_set_int( menu, 8, 2, jx );
	menu_set_int( menu, 9, 2, right_margin );
	menu_set_int( menu, 10, 2, wrap_stat );
	menu_set_int( menu, 11, 2, format_stat );
	menu_set_int( menu, 12, 2, display_tabs );
	menu_set_int( menu, 14, 2, use_format_line == 0 );
	menu_set_int( menu, 15, 2, use_format_line == 1 );
	return_int = menu;
	rm('USERIN^Data_In /HN=1' + return_str);
	if (return_int) {
		doc_mode = menu_item_int( menu, 3, 2 );
		if( menu_item_int( menu, 5, 2 ))
				indent_style = 0;
		else if (menu_item_int( menu, 6, 2 ) )
				indent_style = 1;
		else
				indent_style = 2;
		right_margin = menu_item_int( menu, 9, 2 );
		wrap_stat = menu_item_int( menu, 10, 2 );
		format_stat = menu_item_int( menu, 11, 2 );
		display_tabs = menu_item_int( menu, 12, 2 );
		use_format_line = !menu_item_int( menu, 14, 2 );
		if( !use_format_line )
				rm('SETTABS /RM=' + str(right_margin) + '/TS=' + str( menu_item_int( menu, 8, 2 ) ));
		if(return_int == 11)
				rm('EXTSETUP');
		else if (return_int == 10) {
			refresh = TRUE;
			rm('MEUTIL1^EDFORMAT');
		}
	}
	menu_delete( menu );
}

macro PRINTSTR TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: PRINTSTR

Description:  Sends a string of text to a file or device such as the printer.

Parameters:
							RETURN_STR= The string to send to the printer.
							/G= name of global string to send.  If /G= exists, the global
									will be sent instead of Return_Str.  This one should be used
									to get around the 256 char limit of Return_str.

							/H= File handle.  If < 1, it will default to 4, which is STDPRN.
									This default is not recommended for networks, as it may not
									actually print until ME is exited.  It is the calling macro's
									responsibility to be sure the file or device is open.
							/S=
									0 = Normal print
									1 = Check device status before printing

	Returns:
							Will set ERROR_LEVEL if there is an error, however, it is the
							calling macro's responsibility to process the error.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str Temp_Str[2048] = parse_str("/G=",Mparm_str);
	int Handle,Byte_Count,Error,Retry_Time = 37;

	if (svl(temp_str))
		Temp_str = global_str(temp_str);
	else
		Temp_str = return_str;

	if(  (Global_Int('@PRINT_RETRY_TIME') > 0)  ) {
		Retry_Time = Global_Int('@PRINT_RETRY_TIME') * 18;
	} else if(  (Global_Int('@PRINT_RETRY_TIME') < 0)  ) {
		Retry_Time = 0;
	}
	Handle = Parse_Int('/H=',MParm_Str);
	if(  (Handle < 1)  ) {
		Handle = 4;
	}

/* Check ready status of file or device */
	if(  (Parse_Int('/S=',MParm_Str))  ) {
		Byte_Count = System_Timer + Retry_Time;
CHECK_STATUS:
		R_AX = $4407;
		R_BX = Handle;
		Intr($21);
		if(  ((R_AX & $FF) != $FF)  ) {
			if(  (System_Timer < Byte_Count)  ) {
/* Retry for approx 2 seconds */
				Goto CHECK_STATUS;
			}
			Goto DEVICE_NOT_READY;
		}
/* Change character device to "raw" mode. */
		R_AX = $4400;
		R_BX = Handle;
		Intr($21);
		if(  ((R_Flags & $01) == 0)  ) {
			R_DX = (R_DX | $20) & $FF;
			R_AX = $4401;
			R_BX = Handle;
			Intr($21);
		}
	}

/* Write string to file or device */
	Error = S_Write_Bytes(Temp_Str,Handle,Byte_Count);
	if(  (Error)  ) {
		Error_Level = Error + 3000;
	} else {
		if(  (Byte_Count < Svl(Temp_Str))  ) {
			Error_Level = 3241;
		}
	}
	Goto EXIT;

DEVICE_NOT_READY:
	Error_Level = 2002;

EXIT:

}

void PRN_CONV_TO_HEX(str &t_str) TRANS2 {
  str raw_str[1024] = t_str,
      t_hex_str[2];
  int t_int;
  t_str = "";

  for (t_int = 1; t_int <= svl(raw_str); ++t_int) {
    t_hex_str = Hex_Str(Ascii(str_char(raw_str,t_int)));
    if (svl(t_hex_str) == 1) {
      t_hex_str = "0" + t_hex_str;
    }
    t_str += t_hex_str + " ";
  }

  RETURN();
}

void PRN_FILTER(str &t_str) TRANS2 {
  int filter_char = global_int("@PRN_FILTER_CHAR"),
      t_int,
      t_str_addr = ((seg(t_str) << 16) | ofs(t_str)) + 3;

  if (filter_char == 0) {
    filter_char = 32; // default is a space
  } else if (filter_char == -1) {
    filter_char = 0; // null if global is -1
  }

  for (t_int = 1; t_int <= svl(t_str); ++t_int) {
    if (xpos(str_char(t_str,t_int),"\x0\x1\x2\x3\x4\x5\x6\x7\x8\x9\xA\xB\xC\xD\xE\xF\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F",1)) {
      memb(t_str_addr + t_int, filter_char);
    }
  }
}

macro PRINT_FILE_BLOCK TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: PRINT_FILE_BLOCK

Description:  Sends the current file or currently marked block to a printer
							or file .

Parameters:
							/B= If 1, print block else print whole file
							/X= The upper left column coordinate
							/Y= The upper left row coordinate
							/H= Handle of the printer device or file.  If omitted, will open
									the currently defined printer device.
							/C= Number of copies to print.  Defaults to 1.
							/LN= Print line numbers to the left of each line:
									1=Mode 1.  Numbers begin with 1 on block print
									2=Mode 2.  Numbers reflect line of file on block print

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int ul_col,ul_row,jx,Handle = 0,Copy_Count,Line_Count,
					Line_Numbers,Block,Handle_Specified,LF_Present;
	str temp_string[2048], margin_str[80], sstr[10] ,Term_Str[2];

/* Always use CR,LF as line terminator unless otherwise specified*/
	if ( !Global_Int('@PRN_USE_WIN_TERM' )) {
		Term_Str = Line_Terminator;
	} else {
		Term_Str = '|13|10';
	}
/* If line terminator is only one character, strip off the null character */
	if(  (Str_Char(Term_Str,2) == '|0')  ) {
		Term_Str = Str_Char(Term_Str,1);
	}
	Copy_Count = Parse_Int('/C=',MParm_Str);
	Line_Numbers = Parse_Int('/LN=',MParm_Str);
	Handle_Specified = XPos('/H=',Mparm_Str,1) > 0;
	if(  (Handle_Specified)  ) {
		Handle = Parse_Int('/H=',MParm_Str);
	} else {
		RM('OPEN_CLOSE_FILE /M=1/FN=' + Global_Str('PRINTER_DEVICE'));
		if(  (Error_Level)  ) {
			RM('MEERROR');
			Goto EXIT;
		}
		Handle = Return_Int;
//Send printer init string
		RM('PRINT_CODE_MENU /I=1');
		if (Return_Str != '') {
			Set_Global_Str("@PRINT_CODE",Parse_str("C=",Global_Str("@PRINT_CODE")));
			RM('PRINTSTR /G=@PRINT_CODE/S=1/H=' + Str(handle));
			if (Error_Level) {
				RM('Meerror');
				Goto EXIT;
			}
		}
	}
	Block = Parse_Int('/B=',MParm_Str);

	refresh = false;
	sstr = ' /S=1';
	jx = 1;
	margin_str = '';
	while(  jx < print_margin  ) {
		margin_str = margin_str + ' ';
		++jx;
	}

	ul_col = parse_int('/X=', mparm_str);
	if(  ul_col == 0  ) {
		ul_col = 10;
	}
	ul_row = parse_int('/Y=', mparm_str);
	if(  ul_row == 0  ) {
		ul_row = 6;
	}

	Temp_String = 'PRINTING';
	if(  (Block)  ) {
		if(  (Block_Stat == 0)  ) {
			beep;
			goto NO_BLOCK;
		}
		Temp_String = Temp_String +' BLOCK';
		block_end;
	}

	Put_Box(UL_COL,UL_ROW,UL_COL + 26,UL_ROW + 3,0,m_b_color,Temp_String,True );
	Write('Press <ESC> to abort.',Ul_Col + 1,Ul_Row + 1,0,m_s_Color);
	Mark_Pos;

PRINT_AGAIN:
	if(  (Block)  ) {
		GOTO_LINE( BLOCK_LINE1 );
	} else {
		Tof;
	}
	if (line_numbers > 1) {
		Line_Count = C_Line - 1;
	} else {
		Line_Count = 0;
	}
PRINT_LINE:
	++Line_Count;
	if(  CHECK_KEY  ) {
		if(  KEY1 == 27  ) {
			Goto EXIT;
		}
	}
	Temp_String = Get_Line;
	Tabs_To_Spaces(Temp_String);
	if(  (Block)  ) {
/* Special processing for column and stream block modes */
		if(  (BLOCK_STAT == 2)  ) {
			Temp_String = Copy(Temp_String,BLOCK_COL1,(BLOCK_COL2 - BLOCK_COL1) + 1);
		}
		if(  (BLOCK_STAT == 3)  ) {
			if(  (C_LINE == BLOCK_LINE1)  ) {
				Temp_String = Copy(Temp_String,BLOCK_COL1,2048);
			}
			if(  (C_LINE == BLOCK_LINE2)  ) {
				if(  (C_LINE == BLOCK_LINE1)  ) {
					Temp_String = Copy(Temp_String,1,(BLOCK_COL2 - BLOCK_COL1) + 1);
				} else {
					Temp_String = Copy(Temp_String,1,BLOCK_COL2);
				}
			}
		}
	}
/* Strip out HARD_CR if present */
	if(  (Hard_Cr != '|0')  ) {
STRIP_HARD_CR:
		Jx = XPos(Hard_Cr,Temp_String,1);
		if(  (Jx)  ) {
			Temp_String = Str_Del(Temp_String,Jx,1);
			Goto STRIP_HARD_CR;
		}
	}

/* If this is output from the print formatter, each line will have a line feed
character at the beginning.  We need to deal with this in a very special way */
	Lf_Present = (Str_Char(Temp_String,1) == '|10') + 1;
	if (Line_Numbers) {
		Temp_String = Str_Ins(Margin_Str + Copy(Str(Line_Count) + '        ',1,8),
									Temp_String,Lf_Present) + Term_Str;
	} else {
		Temp_String = Str_Ins(Margin_Str,Temp_String,Lf_Present) + Term_Str;
	}

	Call PRINT_STRING;
	sstr = ' ';
	DOWN;
	if(  (Block)  ) {
		if(  (C_LINE <= BLOCK_LINE2)  ) {
			Goto PRINT_LINE;
		}
	} else {
		if(  (Not(At_Eof))  ) {
			Goto PRINT_LINE;
		}
	}

	--Copy_Count;
	if(  (Copy_Count > 0)  ) {
		Temp_String = '|12';
		Call PRINT_STRING;
		Goto PRINT_AGAIN;
	}
	Goto EXIT;

PRINT_STRING:
	if(  (Sstr == ' ')  ) {
CHECK_STATUS:
/* See if the printer is ready, if not, retry until ready, or the user presses
<ESC>. */
		R_AX = $4407;
		R_BX = handle;
		Intr($21);
		if(  ((R_AX & $00FF) != $FF)  ) {
			if(  CHECK_KEY  ) {
				if(  KEY1 == 27  ) {
					Goto EXIT;
				}
			}
			Goto CHECK_STATUS;
		}
	}
/*
	Return_Str = Temp_String;
*/
	Set_Global_Str("@PRINT_CODE",Temp_String);
	RM('PRINTSTR' + sstr + '/G=@PRINT_CODE/H=' + Str(Handle));
	sstr = ' ';
	if(  (Error_Level)  ) {
		RM('MEERROR^Beeps /C=2');
		RM('MEERROR^ERRMSG');
		Error_Level = 0;
		RM('USERIN^VERIFY /T=Abort printing?/H=PR' + '/BL=ERROR: ' +
			Caps(Return_Str) + '/C=' + Str(Ul_Col + 2) + '/L=' + Str(Ul_Row + 1));
		if(  (Return_Int == 0)  ) {
			Delay(1000);
			Goto PRINT_STRING;
		}
		Goto EXIT;
	}
	RET;

EXIT:
	Kill_Box;
	Goto_Mark;
NO_BLOCK:
	if(  ((Handle_Specified == False) & (Handle > 0))  ) {
		RM('OPEN_CLOSE_FILE /H=' + Str(Handle));
	}
	return_int = 0;
}

macro OPEN_CLOSE_FILE TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: OPEN_CLOSE_FILE

Description:  Opens or closes a file or device.

Parameters:
							/M= Mode	If 1, create new file, if 2 open existing file, if 0,
									close the file.
							/FN= File name.  Required if M > 0.
							/H=n  Handle of file to close (/M=0);

Returns:
							RETURN_INT = the file handle
							Error_Level > 0 if an error occurred.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Open_Mode,Error,Handle;
	str F_Name[80];

	Return_Int = 0;
	Open_Mode = Parse_Int('/M=',MParm_Str);
	F_Name = Parse_Str('/FN=',MParm_Str);
	Handle = 0;
	if(  (Open_Mode == 1)  ) {
/* Create the file */
		Error = S_Create_File(F_Name,Handle);
	} else if(  (Open_Mode == 2)  ) {
		Error = S_Open_File(F_Name,1,Handle);
	} else {
		Error = S_Close_File(Parse_Int('/H=',MParm_Str));
	}

	if(  (Error)  ) {
		Error_Level = 3000 + Error;
	} else {
		Return_Int = Handle;
		Error_Level = 0;
	}

}

macro print TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: PRINT

Description: The PRINT dialog box

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int jx = 0,copies,
			Handle = 0,
			menu = menu_create,
      tpb = persistent_blocks,
      hex_flag,
      filter_flag = (global_int("@PRN_FILTER") != 0)
			;
	persistent_blocks = TRUE;
  if (global_int("@PRN_HEX_MODE")) { // If < 2 do normal print.  If 2 do hex print
    hex_flag = (global_int("@PRN_HEX_MODE") == 2);
  } else {
    hex_flag = ((window_attr & 0x20) != 0);
  }


MENU_AGAIN:
	if ((Global_Int('PRN_LINE_NUMBERS') < 0) or
			(Global_Int('PRN_LINE_NUMBERS') > 2)) {
		set_global_int('PRN_LINE_NUMBERS',0);
	}
	return_str = global_str('PRINTER_TYPE');

	menu_set_item( menu, 1, 'print File ','',	'/L=1/C=1/QK=7/R=10',11,0, 0);
	if(  (Block_Stat)  ) {
		menu_set_item( menu, 2, 'print Block','',	'/L=3/C=1/QK=7/R=11',11,0, 0);
	} else {
		menu_set_item( menu, 2, 'print Block','',	'/L=3/C=1/ATTR=' + str( m_k_color ),10,0, 0);
	}
	menu_set_item( menu, 3, 'eject Page ', 'PRN_EJECT_PAGE', '/L=5/C=1/QK=7/M=1/R=12',11,0, 0);
	menu_set_item( menu, 4, 'Setup printer', 'PRN_CODES',	'/L=7/C=1/QK=1/M=1/R=12',11,0, 0);
	menu_set_item( menu, 5, 'printer Type       ', return_str,'/L=1/W=20/C=25/QK=9/M=PRN_TYPE',8,0, 0);
	menu_set_item( menu, 6, 'printer Device/file', global_str('PRINTER_DEVICE'),	'/L=3/W=20/C=25/QK=9/M=PRN_SET_DEVICE',8,0, 0);
	menu_set_item( menu, 7, 'print Margin       ','',	'/L=5/W=3/C=25/QK=7',1, print_margin, 0);
	menu_set_item( menu, 8, 'Copies             ','', '/L=6/W=3/C=25/QK=1/MIN=1',1, 1, 0);
	menu_set_item( menu, 9, 'Line numbers       ', 'Off(PR%LINE NUMBERS)Mode 1()Mode 2()','/L=7/W=6/C=25/QK=1',3,Global_Int('PRN_LINE_NUMBERS') + 1,0);
  menu_set_item( menu,10, 'print in Hex        ', '','/L=8/C=25/QK=10',13,hex_flag,0);
  menu_set_item( menu,11, 'fIlter control chars', '','/L=9/C=25/QK=2',13,filter_flag,0);
	return_int = menu;
  RM('USERIN^Data_In /HN=1/POSG=@PRPOS@/PRE=PRD/A=4/S=1/#=11/T=PRINT/H=PR/CBT=Done');

	if (return_int) {
		print_margin = menu_item_int( menu, 7, 2 );
		set_global_int('PRN_LINE_NUMBERS',menu_item_int( menu, 9, 2 ) - 1);
		copies = menu_item_int( menu, 8, 2 );
    hex_flag = menu_item_int(menu, 10, 2);
    filter_flag = menu_item_int(menu, 11, 2);
		if ((return_int == 10) || (return_int == 11)) {
			jx = return_int;
			RM('PRN_CHECK_DEVICE');
			RM('PRINT_FILE_BLOCK /H=' + str(return_int) + '/B=' + str(jx == 11) +
          '/LN=' + str(global_int('PRN_LINE_NUMBERS')) + '/C=' + str(copies) +
          "/HEX=" + str(hex_flag) +
          "/FILTER=" + str(filter_flag)
					);
			Goto MENU_AGAIN;
		}
	}
	Handle = global_int('PRINTER_HANDLE');
	if(  (Handle)  ) {
		if(  (Jx)  ) {
/* Send printer termination string */
			RM('PRINT_CODE_MENU /T=1');
			if(  (Return_Str != '')  ) {
				RM('PRINTSTR /S=1/H=' + Str(Handle));
				if(  (Error_Level)  ) {
					RM('Meerror');
				}
			}
		}
		RM('OPEN_CLOSE_FILE /H=' + Str(Handle));
		set_global_int('PRINTER_HANDLE', 0);
	}
	return_int = 100;
	menu_delete( menu );
	persistent_blocks = tpb;
}

macro Prn_Type TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: PRN_TYPE

Description:  Brings up the printer type selection menu

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	rm('SETUP^PRTTYPE');
	return_str = global_str('PRINTER_TYPE');
	return_int = 1;
	update_status_line;
}

macro Prn_Set_Device TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: PRN_SET_DEVICE

Description:  Brings up the printer device selection menu

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	RM('USERIN^DB /PRE=PRN/NDF=1/F=MECONFIG/DPT=PRN_DEV.DB/LO=1/U=1/FV=' +
			global_str('PRINTER_DEVICE') + '/LT=PRINTER DEVICE//FILE/H=PR/NC=1/NE=1');
	if(  (Return_Int)  ) {
/* If they change the printer device, we need to save this in install. */
		if(  (Return_Str != global_str('PRINTER_DEVICE'))  ) {
			Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
		}
		Set_Global_Str('PRINTER_DEVICE',return_str);
		if(  global_int('PRINTER_HANDLE')  ) {
			RM('OPEN_CLOSE_FILE /H=' + Str(global_int('PRINTER_HANDLE')));
			set_global_int('PRINTER_HANDLE', 0);
		}
	}
	Return_Str = global_str('PRINTER_DEVICE');
	return_int = 1;
}

macro Prn_Check_Device TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: PRN_CHECK_DEVICE

Description:  Makes sure the printer device is open

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	if(  (Global_Int('PRINTER_HANDLE') == 0)  ) {
		RM('OPEN_CLOSE_FILE /M=1/FN=' + Global_Str('PRINTER_DEVICE'));
		if(  (Error_Level)  ) {
			RM('MEERROR');
			Return_Int = FALSE;
		} else {
			set_global_int('PRINTER_HANDLE', return_int);
			RM('PRINT_CODE_MENU /I=1');
			if(  (Return_Str != '')  ) {
				Set_Global_Str("@PRINT_CODE",Parse_str("C=",Global_Str("@PRINT_CODE")));
				RM('PRINTSTR /G=@PRINT_CODE/S=1/H=' + Str(global_int('PRINTER_HANDLE')));
				if(  (Error_Level)  ) {
					RM('Meerror');
				}
			}
		}
	}
	Return_Int = Global_Int('PRINTER_HANDLE');
}

macro Prn_Eject_Page TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: PRN_EJECT_PAGE

Description:  Sends a form feed character to the printer

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int e;
	RM('PRN_CHECK_DEVICE');
	if(  return_int  ) {
	/* Move to end of existing file.  This is intended for files only, but it
	seems to generate a benign error code 6.  Unless the error turns out to cause
	a problem, I don't see any need to check to see if the handle is a file or a
	device */
		e = S_Move_File_Ptr(return_int,2,0);

		Return_Str = '|12';
		RM('PRINTSTR /S=1/H=' + Str(return_int));
		if(  (Error_Level)  ) {
			RM('Meerror');
		}
	}
}

macro PRN_CODES TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: PRN_CODES

Description: Brings up a menu of printer codes and allows user to send them
						 directly to the printer

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str tstr;

MENU_AGAIN:
	RM('PRINT_CODE_MENU /C=1');
	if(  (Return_Int > 0)  ) {
		tstr = Parse_Str('C=',Return_Str);
		if(  (tstr != '')  ) {
			RM('PRN_CHECK_DEVICE');
			Return_Str = TStr;
			RM('PRINTSTR /S=1/H=' + Str(return_int));
			if(  (Error_Level)  ) {
				RM('Meerror');
			}
		} else {
			Error_Level = 2010;
			RM('MEERROR /EM=PRINTER CODE FOR: ' + Parse_Str('N=',Return_Str) + ' NOT FOUND');
		}
		Goto MENU_AGAIN;
	}
	return_int = 0;
}

macro PRINT_CODE_MENU TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: PRINT_CODE_MENU

Description:  Provides a menu of printer codes from the currently selected
							printer type.

Parameters:
							 /A=1 will disallow arrow keys to exit this menu.  This one is
							 used by the print menu.
							 /I=1 will only grab the printer initialization string.  No menu
							 will be displayed.
							 /T=1 will only grab the printer termination string.  No menu
							 will be displayed.
							 Note: If /I= and /T= are used simultaneously, it will behave as
							 though only /I= was used.

							 ALSO NOTE:  The parameters /F=1 and /C=1 are no longer valid.

Returns:
	Return_Str:
						If /I= <> 1 and /T= <> 1
							 /N=	The name of the selected code
							 /F=  The print format code
							 /C=  The printer escape sequence for that code

						If /I= = 1
								The printer initialization string.
						If /T= = 1
								The printer terminator string.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Format_Only = Parse_Int('/F=',MParm_Str),
					Code_Only = Parse_Int('/C=',MParm_Str),
					Choice,
					Active_Window = Window_Id,
					Db_Window = 0,
					Menu_Window = 0,
					T_Refresh = Refresh,
					T_Reg_Exp_Stat = Reg_Exp_Stat,
					Data_Start
					;
	str Code_Name[25],
					Help_Str[40] = Parse_Str('/H=',MParm_Str);

	if(  (Help_Str == '')  ) {
		Help_Str = 'PR';
	}
	Return_Str = '';
	Return_Int = -1;

	if(  (Parse_Int('/I=',MParm_Str))  ) {
/* get printer initialization code only, no menu */
		RM('GET_DB_RECORD /F=PRINTERS/GLO=@PRINT_CODE/DBF=N/DPT='
				+ Global_Str('PRINTER_TYPE') + '.PRT/FV=Initialization String');
		if(  (Return_Int == 1)  ) {
			Return_Str = Parse_Str('C=',Global_Str('@PRINT_CODE'));
		} else {
			Return_Str = '';
		}
		Goto EXIT;
	}
	if(  (Parse_Int('/T=',MParm_Str))  ) {
/* get printer termination code only, no menu */
		RM('GET_DB_RECORD /F=PRINTERS/GLO=@PRINT_CODE/DBF=N/DPT='
				+ Global_Str('PRINTER_TYPE') + '.PRT/FV=Termination String');
		if(  (Return_Int == 1)  ) {
			Return_Str = Parse_Str('C=',Global_Str('@PRINT_CODE'));
		} else {
			Return_Str = '';
		}
		Goto EXIT;
	}
/*
	IF ((Format_Only = False) and (Code_Only = False)) THEN
 */
		RM('USERIN^DB /HPT=NONE.PRT/F=PRINTERS/ABT=Select/NOALPHA=1/H=INPR/DPT='
				+ Global_Str('PRINTER_TYPE') +
				'.PRT/LO=1/GLO=@PRINT_CODE/LT=SELECT A PRINTER CODE/DT=CONFIGURE PRINTER CODE/PRE=PD');
		if(  (Return_Int > 0)  ) {
			Return_Str = Global_Str('@PRINT_CODE');
		}
/*
		Goto EXIT;
	ELSE
		RM('SETCONFIG /DB=PRINTERS/T=' + Global_Str('PRINTER_TYPE') + '.PRT');
		Db_Window := Window_Id;
		IF (Return_Int) THEN
			Reg_Exp_Stat := True;
			Eol;
			IF (Search_Fwd('%|||12',0)) THEN
				IF (Cur_Char = '') THEN
					Data_Start := C_Line;
					Create_Window;
					Menu_Window := Window_Id;
					Switch_Win_Id(Db_Window);
					WHILE (Cur_Char = '') DO
						IF (((Code_Only = True) and (Parse_Str('C=',Get_Line) <> '')) or
								((Format_Only = True) and (Parse_Str('F=',Get_Line) <> '')) or
								((Format_Only = False) and (Code_Only = False))) THEN
							Code_Name := Parse_Str('N=',Get_Line);
							Switch_Win_Id(Menu_Window);
							Put_Line(Code_Name);
							Down;
						END;
						Switch_Win_Id(Db_Window);
						Down;
					END;
					Switch_Win_Id(Menu_Window);
					Tof;
					set_global_str('@PCMIPARM_1','/DC=1/C=8/L=1/TP=15/WIN='
													 + str(cur_window));

					RM('USERIN^DATA_IN /PRE=@PCM/#=1/H=' + Help_Str +
							'/T=SELECT A PRINTER CODE/A=2/ABT=Select');
					IF (Return_Int > 0) THEN
						Code_Name := Get_Line;
/*
						Choice := C_Line;
						Switch_Win_Id(Db_Window);
						Goto_Line(Data_Start + Choice - 1);
 */
						RM('GET_DB_RECORD /F=PRINTERS/DBF=N/GLO=@PC/DPT=' +
								Global_Str('PRINTER_TYPE') + '.PRT/FV=' + Code_Name);
						IF (Return_Int) THEN
							Return_Str := Global_Str('@PC');
						END;
					ELSE
						Return_Int := -1;
					END;
					IF (Switch_Win_Id(Menu_Window)) THEN
						Delete_Window;
					END;

				END;
			END;
		END;
	END;
 */
EXIT:
	Switch_Win_Id(Active_Window);
	Refresh = T_Refresh;
	Reg_Exp_Stat = T_Reg_Exp_Stat;
}

macro NOTINC TRANS {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: NOTINC

Description:  Puts up the message that a module is not included.

Parameters:
							/X= The upper left column coordinate
							/Y= The upper left row coordinate
							/T= The name of the option
							/H= The help index string, if help is to be enabled

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int x,y;
	str t_return_str = return_str;
	int  menu = menu_create ;
  menu_set_item( menu, 1, 'This module is not installed, and is included','',
      '/L=1/C=1',10,0, 0);
  menu_set_item( menu, 2, 'only with the Professional package.  If you','',
      '/L=2/C=1',10,0, 0);
  menu_set_item( menu, 3, 'are running Multi-Edit Professional then you','',
      '/L=3/C=1',10,0, 0);
  menu_set_item( menu, 4, 'need to install the module from the Installation','',
      '/L=4/C=1',10,0, 0);
  menu_set_item( menu, 5, 'and Setup -> Module installation dialog box.','',
      '/L=5/C=1',10,0, 0);
  menu_set_item( menu, 6, 'Press F1 for more info on this optional module.','',
      '/L=6/C=1',10,0, 0);
	return_int = menu;
  RM('UserIn^Data_In /HN=1/A=1/#=6' + mparm_str );
	menu_delete( menu );

	return_str = t_return_str;
	return_int = 100;
}
