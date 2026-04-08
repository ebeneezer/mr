macro_file MECOM;
#define MECOM_VERSION "3.00"
#define COM_STAT_COL 32
/*******************************************************************************
												MULTI-EDIT MACRO FILE MECOM

COM - The communications module main macro.
COM_HELP - Calls the help for the com module.
COM_ABOUT - The about screen for MECOM
COM_MAIN_MENU - The top level menu for MECOM.
COM_MSG_FKEY_OFF - Turns message_row and fkey_row off for normal MECOM operation
COM_MSG_FKEY_ON - Turns message_row and fkey_row on for dialog boxes
COM_SCREEN_OP - Performs various screen operations
COM_DIR_SHELL - Enter File Manager(DOS shell) from MECOM.
COM_ERROR - Returns verbose messages according to a MECOM specific error number.
COM_OPEN_PORT - Opens a com port.
COM_PORT_STATUS - Returns useful info about the com port.
COM_FILE - File uploading, downloading and log file.
COM_XFER_BEEP - Beeping for file transfers.
COM_LOG_ERROR - Log file and ASCII upload error routines.
COM_QUIT - Exit prompt for MECOM
COM_ASCII_DOWN - ASCII download
COM_ASCII_UP - ASCII upload
COM_PROTO_MENU - Creates a menu of transfer protocols
COM_SETUP - Installation and setup for MECOM.
COM_TERM_LIST - Pops up a list of terminal types for COM_SETUP.
COM_TERM_WARNING - Warning message for those who add new terminals to the list.
COM_SETUP_PORT - A small menu routine for port specific setup.  Used by COM_SETUP.
COM_PHONE - Phone dialing and hang up.
COM_PROCESS_DIAL - Initializes default values for newly created phone number.
COM_MODEM_OUT - Processes a string for being sent to a modem, then sends it.
COM_SEND_BREAK - Sends a break condition to the com port.
COM_WRITE_L_A - Handles writing to the log and ASCII download file.
COM_INIT - Gets data from MECOM.DB
COM_DUMB - Dumb terminal emulation
COM_ANSI - ANSI terminal emulation
COM_WAIT_FOR_STR - Waits for a string or set of strings from the com port.
COM_SEND_STR - Sends a string to the com port.
COM_CONNECT_WAIT - Waits for a connection after dialing.
COM_CURSOR_OFF - Turns the cursor off.
COM_CURSOR_ON - Turns the cursor on.
COM_SCREEN_DUMP - Sends the current screen to a file or printer.
COM_SEND_MSG - Creates a window to edit a message to send to the com port.
COM_VIEW_FILE - Creates a window to view/edit a file.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

macro COM {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM

Description:	The comunications module main macro.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str tstr[400],
			t_menu_bar_str[128] = menu_bar_str;

	int error, jx, port_num,X1,Y1,X2,Y2,Menu_Choice,Active_Window,Menu_Y,
					Changes_Made,Duplex,Dest_Bs,Baud,Parity,Data_Bits,/*Timer_Handle,*/
					Stop_Bits,Logging,T_Mode,
					T_Status_Row = Status_Row,
					T_Message_Row = Message_Row,
					T_Menu_Bar_Row = Menu_Bar_Row,
					T_Time_Col = Time_Col,
					T_Time_Row = Time_Row,
					original_window = window_id
					;
	char  ch ,Bs_Char;


	T_Mode = Mode;
	Mode = Term;
	Set_Global_Int('Temp_fkey_row',fkey_row);
	Set_Global_Int('MENU_LEVEL',Global_Int('MENU_LEVEL') + 1);

	intr($11);
	set_global_int("COM_MONOCHROME",(r_ax & $0030) == $0030);
	Time_Col = 0;
	Time_Row = 0;
	status_row = 0;
	fkey_row = 0;
	Message_Row = 0;
	Refresh = False;
	Menu_Y = 2;
	Undo_Stat = False;

	Return_Str = '';
	Active_Window = Cur_Window;
/* If we've run MECOM since ME was started.  All these globals should have
values, and therefore it is not neccessary to get them from disk. */
	if ((Global_Str("COM_ASCII_PARAMS") == "") ||
		(Global_Str("COM_LINE_PARAMS") == "") ||
		(Global_Str("COM_TERMINAL_PARAMS") == "") ||
		(Global_Str("COM_MODEM_PARAMS") == "") ||
		(Global_Str("COM_SCREEN_PARAMS") == "") ||
		(Global_Str("COM_COLOR_PARAMS") == "") ||
		(Global_Str("COM_BOX_PARAMS") == "") ||
		(Global_Str("COM_GENERAL_PARAMS") == "")) {
		RM("MECOM^COM_INIT");
	}

	if (Global_Int('COM_DEFAULT_COLORS')) {
/* If someone used the /B or /C on the command line, default the colors */
		Set_Global_Str('Com_Color_Params','/COLOR=/F=15/B=0');
		Set_Global_Int('COM_DEFAULT_COLORS',0);
	}
	if (Global_Int('Com_Port_Open') == 0) {
		Call INIT_PORT;
	}
	Call SET_TERM;
	Text_Color_Vp = Parse_Int('/F=',Global_Str('Com_Color_Params')) +
									(Parse_Int('/B=',Global_Str('Com_Color_Params')) << 4);
	if (Parse_Int('/FS=',Global_Str('Com_Screen_Params')) == True) {
		Call FULL_SCREEN;
	} else {
		Call MAKE_COM_BOX;
	}


	Write_VP("Multi-Edit Communications Module Version " + MECOM_VERSION + '|13|10');
	Set_Global_Int('Com_Cursor_Pos',WhereX_VP | (WhereY_Vp << $08));
	Set_Global_Int('Com_Logging',False);
	Logging = False;
	Set_Global_Int('Com_ASCII_Download',False);
	Set_Global_Str('Com_Log_Str','');
	Set_Global_Int('Com_First_Log',True);
	Set_Global_Str('Com_Log_File',Parse_Str('/LF=',Global_Str('Com_General_Params')));

	if (Global_Int('Com_Port_Open') == False) {
		RM('MECOM^COM_OPEN_PORT /P=0');
	} else {
		RM('MECOM^COM_OPEN_PORT');
	}

	if (Global_Int('Com_Port_Open')) {
		Port_Num = Global_Int('Com_Port_Num');
	} else {
		Port_Num = 0;
	}

	Menu_Choice = 1;

MAIN_LOOP:
	Call WRITE_STATUS;
	error_level = 0;
	RM(Global_Str('COM_TERMINAL_MACRO'));
	if (error_level == 5001) {
		rm('MEERROR');
		goto EXIT;
	}

		Jx = Inq_Key(Key1,Key2,TERM,TStr);
		if (Jx == 1) {
MOUSE_MACRO:
			Set_Global_Int('COM_CHANGES_MADE',0);
			Return_Str = '';
			RM(TStr);
			if (Error_Level) {
				RM('MEERROR');
			}
			if (Parse_Int('/QUIT=',Return_Str)) {
				goto exit;
			}
			if (Global_Int('COM_CHANGES_MADE') != 0) {
				if ((Global_Int('COM_CHANGES_MADE') & $04) != 0) {
/* Terminal */
					Call SET_TERM;
				}
				if ((Global_Int('COM_CHANGES_MADE') & $10) != 0) {
					if (Parse_Int('/FS=',Global_Str('Com_Screen_Params'))) {
						Call FULL_SCREEN;
					} else {
						Kill_Box;
						Return_Str = '/DRC=1';
						Goto MAKE_NEW_BOX;
					}
				}
			}
			if (Parse_Int('/BOX=',Return_Str) == -1) {
MAKE_NEW_SCREEN:
				Call FULL_SCREEN;
			}
			if (Parse_Int('/BOX=',Return_Str) == 1) {
MAKE_NEW_BOX:
				Jx = Cur_Window;
				Switch_Window(Active_Window);
				Fkey_Row = Global_Int('Temp_Fkey_Row');
				Status_Row = T_Status_Row;
				Message_Row = T_Message_Row;
				Refresh = True;
				New_Screen;
				Refresh = False;
				Switch_Window(Jx);
				Fkey_Row = 0;
				Status_Row = 0;
				Message_Row = 0;
				Call MAKE_COM_BOX;
			}
			if (Parse_Int('/CLS=',Return_Str)) {
				Call CLEAR_SCREEN;
			}
			Jx = XPos('/WRITE=',Return_Str,1);
			if (Jx) {
			RM('MECOM^'+ Global_Str('Com_Terminal_MACRO') + ' /DO=' +
				Parse_Str('/WRITE=',Return_Str));
			}

/* This is for running a macro after dialing a phone number. */
			Jx = XPos('/PM=',Return_Str,1);
			if (Jx) {
				RM(Copy(Return_Str,Jx + 4,255));
				if (Error_Level) {
					RM('MEERROR');
				}
			}
			Set_Vp(X1 + 1,Y1 + 1,X2 - 1,Y2 - 1);
			Goto MAIN_LOOP;
		} else {
			if (Key1 == 0) {
				if (Key2 == 250) {
					if (Mou_Last_Y == Menu_Bar_Row) {
/*
These mouse zones are defined for the following status line.  Any mods to the
status line format will require changes to the zone boundries, and the
HELP <F1>   ³MENU <F2>   ³COM2  2400 8 1 N³LOG CLOSED³ANSI    ³10-02-91 04:22pm
															 ³COM2  2400 8N1³LOG OFF³ANSI    ³01-17-90 03:38pm
 */
						if ((mou_last_X < COM_STAT_COL)) {
							push_key(key1,key2);
							TStr = 'MECOM^COM_MAIN_MENU';
							Goto MOUSE_MACRO;
						}
						if ((Mou_Last_X > COM_STAT_COL) &&
							(Mou_Last_X < (COM_STAT_COL + 15))) {
							TStr = 'MECOM^COM_SETUP /TP=4';
							Goto MOUSE_MACRO;
						}

						if ((Mou_Last_X > (COM_STAT_COL + 15)) &&
							(Mou_Last_X < (COM_STAT_COL + 23))) {
							TStr = 'MECOM^COM_FILE /TP=3';
							Goto MOUSE_MACRO;
						}
						if ((Mou_Last_X > (COM_STAT_COL + 23)) &&
							(Mou_Last_X < (COM_STAT_COL + 32))) {
							TStr = 'MECOM^COM_SETUP /TP=3';
							Goto MOUSE_MACRO;
						}
					} else {
/* Cut and paste from the terminal screen */
						push_key(key1,key2);
						rm('MEHELP^ScreenMrk');
					}
				}
			}
		}
	goto main_loop;

/*********************************** SUBROUTINES *******************************/
CLEAR_SCREEN:
	Clr_Vp;
	Set_Global_Int('Com_Cursor_Pos',$101);
	RET;

MAKE_COM_BOX:
	Kill_Box;
	X1 = Parse_Int('/X1=',Global_Str('Com_Box_Params'));
	Y1 = Parse_Int('/Y1=',Global_Str('Com_Box_Params'));
	X2 = Parse_Int('/X2=',Global_Str('Com_Box_Params'));
	Y2 = Parse_Int('/Y2=',Global_Str('Com_Box_Params'));
	if (X1 < 1) {
		X1 = 1;
	}
	if (Y1 < 2) {
		Y1 = 2;
	}
	if (X2 > Screen_Width) {
		X2 = Screen_Width;
	}
	if (Y2 > (Screen_Length - 1)) {
		Y2 = (Screen_Length - 1);
	}
	Set_Global_Str('Com_Box_Params','/BOX=' +
		'/X1=' + Str(X1) +
		'/Y1=' + Str(Y1) +
		'/X2=' + Str(X2) +
		'/Y2=' + Str(Y2)
		);
	Set_Global_Str('Com_Cur_Scrn_Params','/X1=' + Str(X1) + '/Y1=' + Str(Y1) +
																		'/X2=' + Str(X2) + '/Y2=' + Str(Y2));
	Put_Box(X1,Y1,X2,Y2,0,Text_Color_Vp,
					'MULTI-EDIT COMMUNICATIONS MODULE',False);

	Set_Vp(X1 + 1,Y1 + 1,X2 - 1,Y2 - 1);

	Call INIT_STATUS_AND_MENU;

	Call CLEAR_SCREEN;
	Set_Global_Int('Com_Full_Screen',False);
	RET;

FULL_SCREEN:
	Kill_Box;
	X1 = 0;
	Y1 = 1;
	X2 = Screen_Width + 1;
	Y2 = Screen_Length + 1;
	Set_Global_Str('Com_Cur_Scrn_Params','/X1=' + Str(X1) + '/Y1=' + Str(Y1) +
																		'/X2=' + Str(X2) + '/Y2=' + Str(Y2));
	Set_Vp(X1 + 1,Y1 + 1,X2 - 1,Y2 - 1);

	Call INIT_STATUS_AND_MENU;


	Call CLEAR_SCREEN;
	Set_Global_Int('Com_Full_Screen',True);
	RET;

INIT_STATUS_AND_MENU:
		Menu_Bar_Row = 1;
/* Here we are setting up to us ME's built in clock display.  The only special
thing we have to remember is the clock display uses M_T_Color, not Stat1_Color.
Therefore, MECOM's status line must also write with M_T_Color. */
	Time_Col = 65;
	Time_Row = Menu_Bar_Row;
	Draw_Char(COM_STAT_COL,1,Time_Row,M_T_Color,Screen_Width);
	menu_bar_str = '';
	RM('MENU /MN=COM_MAIN/BUILD=1');
	menu_bar_str = copy( menu_bar_str, 1, COM_STAT_COL - 1 );
	draw_menu_bar;
/*
	Menu_Bar_Str = "File Phone Setup User Help";
*/
	RET;

SET_TERM:
	Duplex = Parse_Int('/D=',Global_Str('Com_Terminal_Params'));
	Set_Global_Int('COM_DUPLEX',Duplex);
	Vp_Bs = Parse_Int('/BT=',Global_Str('Com_Terminal_Params'));
	Vp_Wrap = Parse_Int('/W=',Global_Str('Com_Terminal_Params'));
	Vp_Lf = Parse_Int('/CI=',Global_Str('Com_Terminal_Params'));
	Bs_Char = Char(Parse_Int('/BK=',Global_Str('Com_Terminal_Params')));
	RM("GET_DB_RECORD /F=MECOM/NDF=1/DPT=TERMINALS.DB/GLO=COM_TERMINAL_MACRO/DBF=N/FV=" +
			Parse_Str("/T=",Global_Str("Com_Terminal_Params")));
	if (return_int == 1) {
		Set_Global_Str("COM_TERMINAL_MACRO",Parse_Str("M=",Global_Str("COM_TERMINAL_MACRO")));
	} else {
		Set_Global_Str("COM_TERMINAL_MACRO","COM_DUMB");
	}
	RET;

INIT_PORT:
	Set_Global_Int('Com_Port_Num',Parse_Int('/C=',Global_Str('Com_Line_Params')));
	Baud = Parse_Int('/B=',Global_Str('Com_Line_Params'));
	Parity = Parse_Int('/P=',Global_Str('Com_Line_Params'));
	Data_Bits = Parse_Int('/D=',Global_Str('Com_Line_Params'));
	Stop_Bits = Parse_Int('/S=',Global_Str('Com_Line_Params'));
	RET;

WRITE_STATUS:
/* If any mods are made to the format of the status line, you must follow through
with mods to the mouse zones and to the position that the date and time gets
written in COM_DATE_TIME.
*/
	Port_Num = Global_Int('Com_Port_Num');
	TStr = '³COM' +
									Str(Port_Num) + ' ';
	if (Global_Int('Com_Port_Open')) {
		rm("COM_PORT_STATUS");
/* Notice 38400 is out of sequence because it uses a value once used for 150 */
		TStr = TStr +
			Copy('  11038400  300  600 1200 2400 4800 960019200',
			(Parse_Int('/B=',return_str) * 5) + 1,5) + ' ' +
			Str(Parse_Int('/D=',return_str) + 5) +
			Copy('NOE', Parse_Int('/P=',return_str) + 1,1) +
			Str(Parse_Int('/S=',return_str) + 1);
	} else {
		TStr = TStr + 'CLOSED     ';
	}
	TStr = TStr + '³LOG ' + Copy('OFFON ',
		(Global_Int('Com_Logging') * 3) + 1,3) + '³' +
		Copy(Parse_Str('/T=',Global_Str('Com_Terminal_Params')) + '        ',1,8) +
		'³'/* + Date + ' ' + STR_DEL(TIME,6,3) + ' '*/;
	WRITE(TStr,COM_STAT_COL,Menu_Bar_Row,0,M_T_Color);
	if (Global_Int('Com_ASCII_Download')) {
		WRITE( ' ASCII DOWNLOAD ',Com_Stat_Col + 16,Menu_Bar_Row,0,Working_Color);
	}

	RET;

/*******************************************************************************/

error_exit:

exit:
/* This kills the box from the QUIT menu */
	Kill_Box;

	if (!Global_Int('Com_Full_Screen')) {
		Kill_Box;
	}
	Set_Vp(1,1,Screen_Width,Screen_Length);

/* Close the log if open */
	if (Global_Int('COM_LOGGING')) {
		RM("COM_FILE /TP=3");
	}

/* Terminate ASCII download if on */
	if (Global_Int('COM_ASCII_DOWNLOAD')) {
		RM("COM_ASCII_DOWN /QUIT=1");
	}

	Switch_Window(Active_Window);

	Refresh = True;
	status_row = T_status_row;
	fkey_row = Global_Int('Temp_fkey_row');
	Message_Row = T_Message_Row;
	Menu_Bar_Row = T_Menu_Bar_Row;
	menu_bar_str = t_menu_bar_str;

	Time_Row = T_Time_Row;
	Time_Col = T_Time_Col;
	Mode = T_Mode;

/* Get rid of most com related globals */
	Set_Global_Int('COM_FULL_SCREEN',0);
	Set_Global_Int('COM_FIRST_LOG',0);
	Set_Global_Int('TEMP_FKEY_ROW',0);
	Set_Global_Str('Com_Cur_Scrn_Params','');
	Set_Global_Str('COM_LOG_FILE','');
	Set_Global_Str('Partial_Sequence','');
	Set_Global_Int('Partial_Term',0);
	set_global_int("COM_MONOCHROME",0);
	switch_win_id( original_window );
	new_screen;
	Undo_Stat = True;
	Set_Global_Int('MENU_LEVEL',Global_Int('MENU_LEVEL') - 1);
}

macro COM_HELP {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_HELP

Description:	The comunications help.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	Help('MECOM^QR');
}

macro COM_ABOUT TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_ABOUT

Description:	About dialog box for MECOM.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  menu = menu_create ;
	menu_set_item( menu, 1, 'ÚÄÄÄÄÄÄ·  ÚÄÄÄÄ·  ÚÄÄÄÄÄ·  ÚÄÄÄÄÄ·  ÚÄÄÄÄÄÄ·','', '/L=1/C=6/ATTR=' + str(m_s_color),10,0, 0);
	menu_set_item( menu, 2, '³ É¸É¸ º  ³ÉÍÍÍ¼  ³ÉÍÍÍ¸º  ³ÉÍÍÍ¸º  ³ É¸É¸ º','', '/L=2/C=6/ATTR=' + str(m_s_color ),10,0, 0);
	menu_set_item( menu, 3, '³ º³º³ º  ³ÓÄÄ·   ³º   Ô¼  ³º   ³º  ³ º³º³ º','', '/L=3/C=6/ATTR=' + str(m_s_color ),10,0, 0);
	menu_set_item( menu, 4, '³ º³º³ º  ³ÉÍÍ¼   ³º   Ú·  ³º   ³º  ³ º³º³ º','', '/L=4/C=6/ATTR=' + str(m_s_color ),10,0, 0);
	menu_set_item( menu, 5, '³ º³º³ º  ³ÓÄÄÄ·  ³ÓÄÄÄÙº  ³ÓÄÄÄÙº  ³ º³º³ º','', '/L=5/C=6/ATTR=' + str(m_s_color ),10,0, 0);
	menu_set_item( menu, 6, 'ÔÍ¼Ô¼ÔÍ¼  ÔÍÍÍÍ¼  ÔÍÍÍÍÍ¼  ÔÍÍÍÍÍ¼  ÔÍ¼Ô¼ÔÍ¼','', '/L=6/C=6/ATTR=' + str(m_s_color ),10,0, 0);
	menu_set_item( menu, 7, 'Mult-Edit Communications Module V ' + MECOM_VERSION,'','/L=7/C=9',10,0,0);
	menu_set_item( menu, 8, 'Copyright(c) 1989, 1991 by American Cybernetics, Inc. ','', '/L=9/C=2',10,0, 0);

	return_int = menu;
	RM('UserIn^Data_In /HN=1/H=MECOM^*/A=1/#=8/T=');
	menu_delete( menu );
}


#include commenus.s


macro COM_MAIN_MENU {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_MAIN_MENU

Description:	The comunications module top level menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	if (parse_str('/K=', mparm_str) != '') {
		push_key( ascii(parse_str('/K=', mparm_str)), 0 );
	}
	RM("MENU /TP=1/MN=COM_MAIN");
}

macro COM_MSG_FKEY_OFF {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_MSG_FKEY_OFF

Description:	Turns the message and fkey rows off for normal MECOM operation.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	Message_Row = 0;
	FKey_Row = 0;
	Kill_Box;
	Kill_Box;
}

macro COM_MSG_FKEY_ON {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_MSG_FKEY_ON

Description:	Turns the message and fkey rows on for dialog boxes

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
/* we will save the entire lines as box regions for later restoration as we
enable the "rows ".
*/
	Save_Box(1,2,Screen_Width,2);
	Message_Row = 2;
	Save_Box(1,Screen_Length,Screen_Width,Screen_Length);
	FKey_Row = Screen_Length;
}

macro COM_SCREEN_OP {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_SCREEN_OP

Description:	Performs certain screen operations, mostly invoked from the screen
							menu.

Parameters:
							/TP=
										1 - Change from Box display mode to full screen.
										2 - Change from full screen mode to box.
										3 - Clear the screen.
										4 - Clear the screen.  Removes menu boxes from the screen
Returns:
							Return_Str contains special parameters interpreted by the
							communications main macro to invoke certain actions.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int tp = Parse_Int("/TP=",MParm_Str);

	if ((tp == 2) && (!Global_Int('Com_Full_Screen'))) {
		Return_Str = '/CLS=1/BOX=-1';
	} else if ((tp == 1)  && (Global_Int('Com_Full_Screen'))) {
		Return_Str = '/CLS=1/BOX=1';
	} else if ((tp == 3) || (tp == 4)) {
		Text_Color_Vp = Parse_Int('/F=',Global_Str('Com_Color_Params')) |
										(Parse_Int('/B=',Global_Str('Com_Color_Params')) << 4);
		if (tp == 4) {
			Kill_Box;
			Kill_Box;
		}
		Return_Str = '/CLS=1';
	}
}

macro COM_DIR_SHELL TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_DIR_SHELL

Description:	The comunications module call to the DOS shell.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	Refresh = true;
/* run DIRSHELL as normal except disallow file loading */
	RM('DIRSHELL /S=1');
	refresh = false;
}

macro COM_ERROR TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_ERROR

Description:	Returns verbose messages according to a MECOM specific error
							number.

Parameters:
							Return_Int - the error code.

Returns:
							Return_Str contains the error message.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	Return_Str = '#' + Str(Return_Int);

	if (Return_Int == 2) {
		Return_Str = 'Invalid COM port number';
	} else if (Return_Int == 3) {
		Return_Str = 'COM port not open';
	} else if (Return_Int == 6) {
		Return_Str = 'No serial port found';
	} else if (Return_Int == 7) {
		Return_Str = 'Output queue full';
	} else if (Return_Int == 9) {
		Return_Str = 'Com port already open';
	} else if (Return_Int == 10) {
		Return_Str = 'Input queue is empty';
	} else if (Return_Int == 52) {
		Return_Str = 'Invalid XMODEM parameter';
	} else if (Return_Int == 53) {
		Return_Str = 'Unexpected control character';
	} else if (Return_Int == 54) {
		Return_Str = 'Wrong packet number received';
	} else if (Return_Int == 60) {
		Return_Str = 'No startup SOH received';
	} else if (Return_Int == 61) {
		Return_Str = 'Incomplete packet received';
	} else if (Return_Int == 62) {
		Return_Str = 'Overrun error or break received';
	} else if (Return_Int == 63) {
		Return_Str = 'Checksum or CRC failed';
	} else if (Return_Int == 64) {
		Return_Str = 'Remote aborted transfer';
	} else if (Return_Int == 65) {
		Return_Str = 'Local aborted transfer';
	} else if (Return_Int == 70) {
		Return_Str = 'Cannot open file';
	} else if (Return_Int == 71) {
		Return_Str = 'Error writing to file';
	} else if (Return_Int == 72) {
		Return_Str = 'Cannot close file';
	} else if (Return_Int == 80) {
		Return_Str = 'No startup signal received';
	} else if (Return_Int == 81) {
		Return_Str = 'Wrong startup character received';
	} else if (Return_Int == 82) {
		Return_Str = 'Packet acknowledgement failure';
	} else if (Return_Int == 83) {
		Return_Str = 'Receiver rejected last packet';
	} else if (Return_Int == 84) {
		Return_Str = 'Final ACK not received';
	} else if (Return_Int == 85) {
		Return_Str = 'Time out waiting for final ACK';
	} else if (Return_Int == 86) {
		Return_Str = 'Unable to drain input or output queue';
	} else if (Return_Int == 90) {
		Return_Str = 'Cannot open file';
	} else if (Return_Int == 91) {
		Return_Str = 'Error reading file';
	} else if (Return_Int == 92) {
		Return_Str = 'Cannot close file';
	} else if (Return_Int == 200) {
		Return_Str = 'Not true YMODEM-Batch, try XMODEM-1K';
	}
}

macro COM_OPEN_PORT {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_OPEN_PORT

Description:	The comunications module macro to open and initialize a com port.

Parameters:
							/P=  the port that is currently open.  If no port is open, or you
							don't want the currently open port to be closed, set this to 0.
							/RESET= If <> 0 will reset all com port parameters whether com
											port is currently open or not.  Normally, if a com port
											is currently open, only the handshaking issues are
											addressed.  This forces a complete reset.

NOTE:  All other parameters are derived from Global_Str('COM_LINE_PARAMS').

Returns:
							If successful, it will set the following global variables:
								Global_Int('Com_Port_Num')   =   /C=
								Global_Int('Com_Port_Open')  =   True
							If unsuccessful, it will set:
								Global_Int('Com_Port_Open')  =   False

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str Line_Params = Global_Str('Com_Line_Params');
	int Port_Num = Parse_Int('/C=',Line_Params),
					Error,Jx,Already_Open;

	if (Parse_Int('/P=',MParm_Str)) {
		error = close_com(Parse_Int('/P=',MParm_Str));
	}

	error = open_com( port_num, Parse_Int('/IB=',Line_Params),
			Parse_Int('/OB=',Line_Params),
			Parse_Int('/I' + Str(Port_Num) + '=',Line_Params),
			Parse_Int('/A' + Str(Port_Num) + '=',Line_Params));

	if (Error == 9) {
/* If the port is already open, Assert DTR, send a XON character(if applicable),
and get out. */
		if (Parse_Int("/RESET=",MParm_Str)) {
			error = 0;
		} else {
			error = set_com( port_num, 13,3);
			if (Parse_Int('/XON=',Line_Params)) {
				error = write_com(port_num,'|17', jx);
			}
			Goto EXIT;
		}
	}
	if (error != 0) {
		Set_Global_Int('Com_Port_Open',False);
		Error_Level = 6000 + Error;
		RM('MEERROR /EM=Error opening port ' + str(port_num));
		Goto EXIT;
	} else {
		Set_Global_Int('Com_Port_Num',Port_Num);
		Set_Global_Int('Com_Port_Open',True);
	}
	error = set_com( port_num, 1,Parse_Int('/B=',Line_Params));
	error = set_com( port_num, 2,Parse_Int('/P=',Line_Params));
	error = set_com( port_num, 3,Parse_Int('/D=',Line_Params));
	error = set_com( port_num, 4,Parse_Int('/S=',Line_Params));
	error = set_com( port_num, 5,Parse_Int('/XON=',Line_Params));
	error = set_com( port_num, 6,Parse_Int('/XON=',Line_Params));
/* This is CTS.  It is enabled if RTS is enabled. */
	error = set_com( port_num, 9,Parse_Int('/RTS=',Line_Params));

	error = set_com( port_num, 12,3);
	error = set_com( port_num, 13,3);
	error = set_com( port_num, 14,(Parse_Int('/DTR=',Line_Params) == 0) | ((Parse_Int('/RTS=',Line_Params) == 0) << 1));
	error = set_com( port_num, 15,0);
	if (Parse_Int('/T' + Str(Port_Num) + '=',Line_Params)) {
		if (Global_Int('!Com_Modem' + Str(Port_Num) + '_Init') == 0) {
			Return_Str = Parse_Str('/I=',Global_Str('Com_Modem_Params')) + '|13';
			RM('COM_MODEM_OUT');
			Set_Global_Int('!Com_Modem' + Str(Port_Num) + '_Init',True);
		}
	}

EXIT:
}

macro COM_PORT_STATUS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_PORT_STATUS

Description:	Everything you always wanted to know about the currently open
							com port *


*but were afraid to ask.

Returns:
							Return_Int = 0 if com port is open
													 3 if com port is not open - this corresponds to
														 MECOM's standard com port not open error code.

							Return_Str contains a string of parsable parameters with the
												 following:
													 /C= the currently open com port number

													 The following 4 parameters are interpreted in
													 accordance with the documentation associated with
													 the macro function SET_COM()
													 /B= baud rate
													 /P= parity
													 /D= data bits
													 /S= stopt bits

													 /XONXOFF= XON - XOFF handshaking in use
													 /CTSRTS= CTS - RTS handshaking in use
													 /DSRDTR= DSR - DTR handshaking in use
													 /IQ= The size of the input queue.  Useful for
																checking whether there are characters waiting
																to be read.
													 /OQ= The size of the output queue.  Useful for
																checking to see if all characters sent with a
																WRITE_COM() function have actually been sent
																out the com port.
													 /CTS= Current CTS state
													 /DSR= Current DSR state
													 /RI=  Current Ring Indicator state
													 /CD=  Current Carrier Detect state
													 /PSW= Port status word.  A 16 bit word with the
																 following flags:
																 0 - Input data lost due to being filled up.
																 1 - Input queue size is negative (this should
																		 NEVER happen)
																 4 - Parity error encountered
																 5 - Overrun error detected
																 6 - Framing error detected
																 7 - Break signal detected
																12 - Remote has sent an XOFF
																13 - Remote has sent an XON
																14 - An XOFF has been sent to remote
																15 - An XON has been sent to remote



							 (C) Copyright 1991 by American Cybernetics, Inc.
******************************************************************************/
/*
NOTE: This macro makes extensive use of undocumented interrupts that call
the Blaise Turbo Asynch com port routines.  These interrupts will remain
undocumented for the simple reason that these interrupts will eventually
become macro commands, as the need for portablilty becomes a reality.
*/

	int t_int,
			ax,
			msr,
			port_num = global_int("com_port_num")
	;

	str param[10]
	;

	ax = (Port_Num << 8) | 5;

	return_str = "";

/* Baud Rate */
	R_BX = 1;
	Call RETOPTION;
/* On this first one, check to make sure com port is open */
	return_int = R_AX & $FF;
	if (return_int) {
		return_int = 3;
		goto EXIT;
	}
	return_str = "/C=" + str(port_num);
	param = "B";
	call ADD_PARAM;

/* Parity */
	R_BX = 2;
	call RETOPTION;
	param = "P";
	call ADD_PARAM;

/* Data Bits */
	R_BX = 3;
	call RETOPTION;
	param = "D";
	call ADD_PARAM;

/* Stop Bits */
	R_BX = 4;
	call RETOPTION;
	param = "S";
	call ADD_PARAM;

/* XON - XOFF */
	R_BX = 5;
	call RETOPTION;
	param = "XONXOFF";
	call ADD_PARAM;

/* CTS - RTS */
	R_BX = 9;
	call RETOPTION;
	param = "CTSRTS";
	call ADD_PARAM;

/* DSR - DTR */
	R_BX = 14;
	call RETOPTION;
	param = "DSRDTR";
	t_int = (t_int & 1) == 0;
	call ADD_PARAM;

/* read modem status register for current CTS DSR RI and CD */
	R_BX = 18;
	call RETOPTION;
	msr = t_int;
	param = "CTS";
	t_int = (msr & $10) > 0;
	call ADD_PARAM;
	param = "DSR";
	t_int = (msr & $20) > 0;
	call ADD_PARAM;
	param = "RI";
	t_int = (msr & $40) > 0;
	call ADD_PARAM;
	param = "CD";
	t_int = (msr & $80) > 0;
	call ADD_PARAM;

/* Input Queue Size */
	ax = (Port_Num << 8) | 3;
	R_BX = $0000;
	R_CX = 0;
	call RETOPTION;
	t_int = R_BX;
	param = "IQ";
	call ADD_PARAM;
	t_int = R_DX;
	param = "PSW";
	call ADD_PARAM;

/* Output Queue Size */
	ax = (Port_Num << 8) | 2;
	R_BX = 0;
	R_CX = 0;
	call RETOPTION;
	t_int = R_BX;
	param = "OQ";
	call ADD_PARAM;


	goto EXIT;

RETOPTION:
	R_AX = ax;
	Intr($66);
	t_int = R_BX >> 8;
	RET;

ADD_PARAM:
	return_str = return_str + "/" + param + "=" + str(t_int);
	RET;


EXIT:
}

macro COM_FILE {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_FILE

Description:	The comunications module macro for uploading, downloading, and
dealing with the log file.

Parameters:
							/MY= The upper Y coordinate for the menu box.  Defaults to 3.
							/TP=
									 1		=			download a file
									 2		=			upload a file
									 3		=			open/close log file

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int Menu_Y,Type,Error,Port_Num,Cursor_X,Temp_Cursor_X,Temp_Cursor_Y,
					t_int,count,logging,Active_Window;
	str Protocol[20],Tstr,t_dir_entry,Xfer_Direction[4],Command_Line,XFer_File[40];

	t_int = Parse_Int('/KB=',MParm_Str);
	while (t_int) {
		Kill_Box;
		--t_int;
	}
	char T_Char;
	port_num = global_int('COM_PORT_NUM');
	XFer_File = '';
	Temp_Cursor_X = WhereX;
	Temp_Cursor_Y = WhereY;
	Menu_Y = Parse_Int('MY=',MParm_Str);
	if (Menu_Y < 1) {
		Menu_Y = 3;
	}

/* Look for special parameters */
	Type = Parse_Int('/TP=',MParm_Str);
	if ((Type == 1) | (Type == 2)) {
		RM('MECOM^COM_PROTO_MENU /LO=2/X=15/Y=' +  Str(Menu_Y + 2 + Type)
		+ '/DU=' + Str(Type - 1));
		if (Return_Int > 0) {
			if (Type == 1) {
				Return_Str = '';
				Xfer_Direction = 'DOWN';
			} else {
				Return_Str = Parse_Str('/UP=',Global_Str('Com_General_Params'));
				Xfer_Direction = 'UP';
			}
			TStr = Global_Str('COM_PROTOCOL');
			if (Parse_Int('PF=',Tstr) < 2) {
/* If no prompting for filename is configured, skip over that. */
				Return_Str = '';
				Goto SKIP_PROMPT;
			}
			if (Parse_Int('PF=',Tstr) == 2) {
SINGLE_PROMPT:
				Push_Key(0,79);
				RM('MEUTIL1^FILE_PROMPT /H=MECOM^*/T=FILE TO ' + xfer_direction + 'LOAD');
				if (Return_Int == 0) {
					Goto ABORT_XFER;
				}
			}
			if (Parse_Int('PF=',Tstr) == 3) {
				Active_Window = Window_Id;
				Create_Window;
				Return_Str = 'MEXFER.LST';
				RM('Makeuserpath');
				File_Name = Return_Str;
				Xfer_File = Return_Str;
				File_Changed = False;

MULTI_PROMPT:
				Set_Global_Str('@XFER_EV!','/T=File Manager/KC=<F2>/W=13/K1=0/K2=60/R=100');
				RM('USERIN^EDITWINDOW /X=9/T=CREATE A LIST OF FILES TO UPLOAD/Y='
						+ Str(Menu_Y + 2 + Type) + '/W=40/L=10/EV1=@XFER_EV!');

				if (return_int == 100) {
					RM("COM_DIR_SHELL");
					if (Return_int) {
						t_dir_entry = dir_entry;
						Dos_Home;

						t_int = 0;
						count = 0;
						while (t_int < Dir_Total) {
							++t_int;
							if (File_Marked) {
								Mark_File;
								++count;
								Return_str = Dir_Entry;
								Call INSERT_DIR_ENTRY;
							}
							DOS_RIGHT;
						}
						if (!count)
							Call INSERT_DIR_ENTRY;
/* move back to where we were in the dirshell */
						Dos_Home;
						t_int = 0;
						while ((T_dir_entry != dir_entry) && (t_int < Dir_Total)) {
							DOS_RIGHT;
							++t_int;
						}
					}
					goto MULTI_PROMPT;
				}
				if (!File_Changed) {
					Delete_Window;
					if (Switch_Win_Id(Active_Window)) {
					}
					Goto ABORT_XFER;
				}
				RM('VERIFY /X=9/T=Continue transfer?');
				if (return_int == 0) {
					Delete_Window;
					goto abort_xfer;
				}
/* DSZ requires a CR/LF at EOF */
				Eof;
				TEXT('|13|10');
				Save_File;

				Error = Error_Level;
				Delete_Window;
				Switch_Win_Id(Active_Window);

				if (Error) {
					Error_Level = Error;
					RM('MEERROR');
					Goto ABORT_XFER;
				}
			}
SKIP_PROMPT:
				Command_Line = Parse_Str('CL=',TStr);
				if (Command_Line != '') {
					if (XPos(' ',Command_Line,1) == 0) {
						Command_Line = Command_Line + ' ';
					}
					T_Char = Char(Parse_Int('IC=',TStr));
					if (T_Char != '|0') {
						error = write_com(Port_Num,T_Char,t_int);
					}

					RM(Command_Line + '/DIR=' + Xfer_Direction + '/X=' +
						Str(Temp_Cursor_X) + '/Y=' + Str(Temp_Cursor_Y) + '/PRO=' +
						Parse_Str('PN=',Tstr) + '/FN=' + Return_Str + '/BATCH=' +
						Xfer_File);
					if (Error_Level) {
						RM('MEERROR');
					}
				}
ABORT_XFER:
				Tstr = Global_Str('Com_Cur_Scrn_Params');
		}
	}

	if (Type == 3) {
		Logging = Global_Int('COM_LOGGING');
		if (logging == 0) {
COM_LOG_PROMPT:
			Return_Str = Global_Str('Com_Log_File');
			RM('MEUTIL1^FILE_PROMPT /NHA=1/H=MECOM^FILLOG/T=OPEN LOG FILE');
			if (RETURN_INT) {
				if (Return_Str == '') {
					Beep;
					Goto COM_LOG_PROMPT;
				}
				if (File_Exists(Return_Str)) {
					RM('USERIN^HISTORY_LIST /M=1/HISTORY=FILE_HISTORY');
					RM('USERIN^XMENU /B=1/T=1/X=16/Y=' + Str(Menu_Y + 7) + '/S=1/L=LOG FILE EXISTS' +
						'/M=Append(MECOM^FILLOG)Overwrite()aBort()');
					if ((Return_Int < 1) | (Return_Int == 3)) {
						Goto ABORT_LOG_OPEN;
					}
					if (Return_Int == 1) {
						Error = S_Open_File(Return_Str,1,t_int);
						if (Error) {
							Return_Int = Error;
							RM('COM_LOG_ERROR');
							Goto ABORT_LOG_OPEN;
						}
						Error = S_Move_File_Ptr(t_int,2,0);
						if (Error) {
							Return_Int = Error;
							RM('COM_LOG_ERROR');
							Goto ABORT_LOG_OPEN;
						}
					 Goto APPEND_LOG;
					}
					if (Return_Int == 2) {
						Goto NEW_LOG;
					}
				} else {
NEW_LOG:
					Error = S_Create_File(Return_Str,t_int);
					if (Error) {
						Return_Int = Error;
						RM('COM_LOG_ERROR');
						Goto ABORT_LOG_OPEN;
					}
				}
APPEND_LOG:
				Set_Global_Int('Com_Log_Handle',t_int);
/* If we are presently ASCII downloading, dump what's in the log buffer to the
ASCII file before initializing to null */
				if (Global_Int('Com_ASCII_Download')) {
					RM('COM_WRITE_L_A');
				}

				Set_Global_Str('Com_Log_Str','');
				Set_Global_Str('Com_Log_File',Return_Str);
				Set_Global_Int('Com_Logging',True);
				Set_Global_Int('Com_First_Log',False);
				Goto EXIT;
			} else {
ABORT_LOG_OPEN:
				Goto EXIT;
			}
		}
		if (Logging) {
			RM('COM_WRITE_L_A');
			if (Return_Int == -1) {
				Goto ABORT_LOG_CLOSE;
			}
			Error = S_Close_File(Global_Int('Com_Log_Handle'));
			if (Error) {
				Return_Int = Error;
				RM('COM_LOG_ERROR');
			}
ABORT_LOG_CLOSE:
			Set_Global_Int('Com_Logging',False);
		}
	}
	Goto EXIT;

INSERT_DIR_ENTRY:
	eof;
	if (c_col > 1) {
		down;
		goto_col(1);
	}
	Text(return_str);
	RET;

EXIT:
}

macro COM_XFER_BEEP {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_XFER_BEEP

Description:	Called by any file transfer macro.  Determines if the "Beep on
							uploads and downloads" option is enabled and beeps if it is.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	if (Parse_Int('/A=',Global_Str('Com_General_Params')) != 0) {
		RM('MEERROR^BEEPS /C=2/D=500/F=250/P=250');
	}
}

macro COM_LOG_ERROR {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_LOG_ERROR

Description:	An error handler for disk I/O errors relating the log or ASCII
							downloads.

Parameters:
							/A=	If 1 then error message will be for ASCII upload else for
									Log.
							Return_Int = the error number.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	if (Parse_Int('/A=',MParm_Str)) {
		Return_Str = 'ASCII Upload';
		S_Close_File(Global_Int('Com_ASCII_Handle'));
		Set_Global_Int('Com_ASCII_Download',False);
	} else {
		Return_Str = 'Log';
		S_Close_File(Global_Int('Com_Log_Handle'));
		Set_Global_Int('Com_Logging',False);
	}
	Set_Global_Str('Com_Log_Str','');
	Error_Level = Return_Int;
	if (Return_Int == 241) {
		RM('MEERROR /EM=' + Copy('ASCII UploadLog',((Parse_Int('/A=',MParm_Str) == 0) * 12) + 1,12) + ' file disk full error.  File has been closed.');
	} else {
		RM('MEERROR /EM=' + Copy('ASCII UploadLog',((Parse_Int('/A=',MParm_Str) == 0) * 12) + 1,12) + ' file disk error #' + str(3000 + Return_Int) + '.  File has been closed.');
	}

}

macro COM_QUIT {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_QUIT

Description:	The comunications module quit prompt.

Parameters:
							/X=	The upper left corner X coordinate of the menu box.
							/Y=	The upper left corner Y coordinate of the menu box.

Returns:
							Return_Str = '/QUIT=1' if <ESC> is not pressed.  This return_Str is
							to tell the com main program to exit the com module.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Port_Num,
			x = parse_int('/X=',MParm_Str),
			y = parse_int('/Y=',MParm_Str),
			menu = menu_create;
			;
	if (!x) {
		x = 3;
	}
	if (!y) {
		Y = 13;
	}
	Return_Str = '';

	menu_set_item( menu, 1, 'Yes','','/H=MECOM^QUI',0,0,0);
	menu_set_item( menu, 2, 'No','','/H=MECOM^QUI',0,0,0);
	return_int = menu;
	RM('USERIN^SUBMENU /HN=1/#=2/S=1/L=CLOSE COM PORT?/X=' + Str(X) +
		'/Y=' + Str(Y));

	if (Return_Int > 0) {
		Port_Num = Global_Int('Com_port_num');
		if (Return_Int == 1) {
			close_com(Port_Num);
		} else {
/* If port is to remain open, do any neccessary handshaking */
/* If software handshaking is enabled, send an XOFF */
			if (Parse_Int('/XON=',Global_Str('Com_Line_Params'))) {
				write_com(port_num,'|19',x);
			}
/* If hardware handshaking(CTS/RTS) is enabled, deassert RTS, leave DTR asserted */
			set_com( port_num, 13,1 | ((Parse_Int('/RTS=',Global_Str('Com_Line_Params')) == 0) << 1));
		}
		if (Return_Int > 0) {
			Return_Str = '/QUIT=1';
		}
		Set_Global_Int('Com_Port_Open',(Return_Int != 1));
	}
	menu_delete(menu);
}

macro COM_ASCII_DOWN TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_ASCII_DOWN

Description:	The comunications module ASCII download macro.

Parameters:
							/QUIT= If 1, then terminate the download. If 0, then begin.
							/APPEND= If 1, then append the file rather than overwrite.
							/OVERWRITE= Unconditionally overwrites without prompting.
							Return_Str= The file name.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int ASCII_Handle,Error,Bytes_Written;
	str Name[80];

/* We are going use the log file buffer, so if it is active, save and flush. */
	if (Global_Int('Com_Logging')) {
		RM('COM_WRITE_L_A');
		if (Return_Int == -1) {
			Goto EXIT;
		}
	}
	if (Parse_Int('/QUIT=',MParm_Str)) {
		if (Global_Int('Com_ASCII_Download')) {
			RM('COM_WRITE_L_A');
			Error = S_Close_File(Global_Int('Com_ASCII_Handle'));
			if (Error) {
				Return_Int = Error;
				RM('COM_LOG_ERROR /A=1');
			}
			Set_Global_Int('Com_ASCII_Handle',0);
			Set_Global_Int('Com_ASCII_Download',False);
			Set_Global_Str('COM_ASCII_FILENAME','');
		}
/* If they pressed the ASCII download terminate key, but were not downloading, pass
the keystroke to the com port if it is a meaningful keystroke */
		if (Key1 != 0) {
			error = write_com(Global_int('Com_Port_Num'),Char(Key1),Bytes_Written);
		}

		Goto EXIT;
	}
	if (File_Exists(Return_Str)) {
			if (Parse_Int('/APPEND=',MParm_Str)) {
				Error = S_Open_File(Return_Str,1,Ascii_Handle);
				if (Error) {
					Return_Int = Error;
					RM('COM_LOG_ERROR /A=1');
					Goto EXIT;
				}
				Error = S_Move_File_Ptr(ASCII_Handle,2,0);
				Goto APPEND;
			} else if (Parse_Int('/OVERWRITE=',MParm_Str) == False) {
				Name = Return_Str;
				RM('userin^VERIFY /L=10/C=10/H=MECOM^FILLOG/T=The file ' + Caps(Truncate_Path(Return_Str)) + ' already exists.  Overwrite?');
				Return_Str = Name;
				if (Return_Int != 1) {
					Goto EXIT;
				}
			}
	}
	Error = S_Create_File(Return_Str,ASCII_Handle);
APPEND:
	if (Error) {
		Return_Int = Error;
		RM('COM_LOG_ERROR /A=1');
		Goto EXIT;
	}

	Set_Global_Int('Com_ASCII_Handle',ASCII_Handle);
	Set_Global_Int('Com_ASCII_Download',True);
	Set_Global_Str('COM_ASCII_FILENAME',Return_Str);

EXIT:
	Set_Global_Str('Com_Log_Str','');
	Return_Int = 100;

}

macro COM_ASCII_UP TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_ASCII_UP

Description:	The comunications module ASCII upload macro.

Parameters:
							/X= The X coordinate for the cursor to be positioned during the
									upload.
							/X= The Y coordinate for the cursor.

							/FN= The file name to upload.

Returns:
							Return_Int = Error level if an error occurred.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int Timeout_Count,Port_Num,Error,Jx,Carriage_Return,Line_Feed,Pace_Char,
					Line_Pacing,Char_Pacing,Expand_Blanks,Echo,Added_LF,Out_Pointer;
	int Buf_Length,CR_Pos,Pointer,File_Handle,Partial;
	str File_Buf[2048],F_Name,Line_Out[2048],In_Buf,Term_Mac[25];
	char Ch, In_Ch;

	GotoXY(Parse_Int('/X=',MParm_Str),Parse_Int('/Y=',MParm_Str));
	Port_Num = Global_int('Com_Port_Num');
	Carriage_Return = Parse_Int('/CU=',Global_Str('Com_Ascii_Params'));
	Line_Feed = Parse_Int('/LU=',Global_Str('Com_Ascii_Params'));
	Line_Pacing = Parse_Int('/LP=',Global_Str('Com_Ascii_Params')) * 10;
	Pace_Char = Parse_Int('/PC=',Global_Str('Com_Ascii_Params'));
	Char_Pacing = Parse_Int('/CP=',Global_Str('Com_Ascii_Params'));
	Expand_Blanks = Parse_Int('/S=',Global_Str('Com_Ascii_Params'));
	Echo = Parse_Int('/E=',Global_Str('Com_Ascii_Params'));
	In_Buf = '';
	Term_Mac = 'MECOM^' + Global_Str("COM_TERMINAL_MACRO") + ' /DO=';

	if (S_Open_File(Parse_Str('/FN=',MParm_Str),0,File_Handle)) {
		Return_Int = 90;
		Goto OPEN_ERROR;
	}
MORE_FILE:
	if (S_Read_Bytes(File_Buf,File_Handle,2048)) {
		Return_Int = 91;
		Goto ABORT;
	}
	Buf_Length = Svl(File_Buf);

	Call PARSE_BUF;

	if (Buf_Length == 2048) {
		Goto MORE_FILE;
	}
	Goto DONE;

PARSE_BUF:
	Pointer = 1;

MORE_LINES:
	CR_Pos = XPos('|13',File_Buf,Pointer);
	if (Cr_Pos == 0) {
		CR_Pos = Buf_Length;
		Partial = True;
	} else {
		Partial = False;
		if (XPos('|10',File_Buf,Cr_Pos) == (Cr_Pos + 1)) {
			++Cr_Pos;
		}
	}

	Line_Out = Copy(File_Buf,Pointer,Cr_Pos - Pointer + 1);

	if (Expand_Blanks) {
/* If this is a blank line, then expand it if Expand_Blanks is turned on */
		if (Str_Char(Line_Out,1) == '|13') {
			Line_Out = ' ' + Line_Out;
		}
	}

/* If any filtering needs to be done, do it. */
	Added_Lf = 0;
	if (Carriage_Return > 0) {
FILTER_CR:
		Jx = XPos('|13',Line_Out,1);
		if (Jx) {
			if (Carriage_Return == 2) {
/* Strip CR */
				Line_Out = Str_Del(Line_Out,Jx,1);
				Goto FILTER_CR;
			}
			if (Carriage_Return == 1) {
/* Add a LF */
				Line_Out = Line_Out + '|10';
/* Use this as a flag to indicate to the LF filtering not to process */
				Added_Lf = SVL(Line_Out);
			}
		}
	}
	if (Line_Feed > 0) {
FILTER_LF:
		Jx = XPos('|10',Line_Out,1);
		if (Jx > Added_Lf) {
			if (Line_Feed == 2) {
/* Strip LF */
				Line_Out = Str_Del(Line_Out,Jx,1);
				Goto FILTER_LF;
			}
			if (Line_Feed == 1) {
/* Add CR */
				Line_Out = Str_Ins('|13',Line_Out,Jx + 1);
			}
		}
	}

	if (Echo) {
		RM(Term_Mac + Line_Out);
	}

	if (Char_Pacing > 0) {
		Call SEND_CHARS;
	} else {
SEND_LINE:
		error = write_com(port_num,Line_Out,Jx);
		if (Check_Key) {
			if ((Key1 == 27) | (Key1 == 24)) {
				Return_Int = 65;
				Goto ABORT;
			}
		}
		if (Jx < Svl(Line_Out)) {
/* If the whole line didn't get sent, try sending the remainder of the line */
			Line_Out = Copy(Line_Out,Jx + 1,2048);
			Goto SEND_LINE;
		}
	}

	Pointer = Cr_Pos + 1;
	Delay(Line_Pacing);

	if (Pace_Char) {
		Call CHECK_FOR_PACE_CHAR;
	} else {
		Call CHECK_FOR_REMOTE;
	}

	if (Pointer < Buf_Length) {
		Goto MORE_LINES;
	}
	RET;

SEND_CHARS:
	Out_Pointer = 1;
	while (Out_Pointer < Svl(Line_Out)) {
CHAR_AGAIN:
		error = write_com(port_num,Str_Char(Line_Out,Out_Pointer),Jx);
		if (Check_Key) {
			if ((Key1 == 27) | (Key1 == 24)) {
				Return_Int = 65;
				Goto ABORT;
			}
		}
		Delay(Char_Pacing);
		Call CHECK_FOR_REMOTE;
		if (Jx == 0) {
			Goto CHAR_AGAIN;
		}
		++Out_Pointer;
	}
	error = write_com(port_num,Str_Char(Line_Out,Out_Pointer),Jx);
	if (Partial) {
		Call CHECK_FOR_REMOTE;
	}
	Ret;

CHECK_FOR_REMOTE:
		while (read_com(port_num, In_Ch) == 0) {
			if (Svl(In_Buf) > 253) {
				RM(Term_Mac + In_Buf);
				In_Buf = In_Ch;
			} else {
				In_Buf = In_Buf + In_Ch;
			}
			Delay(Char_Pacing);
		}
		if (Svl(In_Buf)) {
			RM(Term_Mac + In_Buf);
		}
		In_Buf = '';
		RET;

CHECK_FOR_PACE_CHAR:
	if (Partial) {
		RET;
	}
WAIT:
	if (Check_Key) {
		if ((Key1 == 27) | (Key1 == 24)) {
			Return_Int = 65;
			Goto ABORT;
		}
	}
	if (read_com(port_num, In_Ch) == 0) {
		if (In_Ch == Char(Pace_Char)) {
			if (Svl(In_Buf)) {
				RM(Term_Mac + In_Buf);
			}
			In_Buf = '';
			Ret;
		} else {
			if (Svl(In_Buf) > 253) {
				RM(Term_Mac + In_Buf);
				In_Buf = In_Ch;
			} else {
				In_Buf = In_Buf + In_Ch;
			}
		}
	}
	Goto WAIT;

DONE:
	Call CHECK_FOR_REMOTE;
	Return_Int = False;

ABORT:
/* Close the file */
	Error = S_Close_File(File_Handle);

OPEN_ERROR:
	if (Return_Int) {
		Error_Level = Return_Int;
		RM('COM_ERROR');
		RM('MEERROR /EM=' + Return_Str);
	}

	Return_Str = '/DRC=1';
}

macro COM_PROTO_MENU {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_PROTO_MENU

Description:	This macro creates a special menu of file transfer protocols.

Parameters:
							/DU= If 0 then download.  If 1 then upload.
							/LO= List only.  If 1, just bring up the menu.  Don't allow
									 editing of menu choices.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int L_Mode = Parse_Int('/LO=',MParm_Str);
	str Direction[8], tstr[30];

	Direction = Copy('DOWNLOADUPLOAD',(Parse_Int('/DU=',MParm_Str) * 8) + 1,8);
	if (L_Mode) {
		TStr = '/ABT=Select';
	} else {
		TStr = '/CBT=Done';
	}
	if (Direction == 'DOWNLOAD') {
		TStr = TSTr + '/H=MECOM^SETDNL';
	} else {
		TStr = TSTr + '/H=MECOM^SETUPL';
	}

	RM('USERIN^DB /GLO=COM_PROTOCOL/LO=' + Str(L_Mode) +
		'/PRE=PROTO/NOALPHA=1/NDF=1/ENC=1/F=MECOM/DPT=' +
		Direction + '.DB/LT=' +
		Direction + ' PROTOCOLS/HPT=DOWNLOAD.DB/DT=' + Direction +
		' PROTOCOL MACRO SETUP' + Tstr);

	if (Return_Int == 0) {
		Set_Global_Str('COM_PROTOCOL','');
	}

EXIT:

}

macro COM_SETUP TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_SETUP

Description:	The comunications module installation and setup macro.

Parameters:
							/X=	The upper left corner X coordinate reference point.
							/Y=	The upper left corner Y coordinate reference point.
							/TP=
											1  -  General parameters
											2  -  modem parameters
											3  -  terminal parameters
											4  -  line parameters
											5  -  screen parameters
											6  -  download protocols
											7  -  upload protocols
											8  -  Ascii parameters
											10  -  save setup

Returns:
							Global_Int('COM_CHANGES_MADE')
							If not 0 then parse out the following bits:
								Bit 0 = changed general parameters
								Bit 1 = changed modem parameters
								Bit 2 = changed terminal parameters
								Bit 3 = changed port parameters
								Bit 4 = changed screen parameters
								Bit 5 = changed ascii parameters
							Return_Int
								Only useful to SUBMENU.  Tells whether or not to kill all the
								boxes.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str X_Str[2],Y_Str[2],B_Str[10],Direction[8],G_Str,H_Str[30],MStr;
	int jx,
			Sub_Choice,
			Menu_X = Parse_Int('/X=',MParm_Str),
			Menu_Y = Parse_Int('/Y=',MParm_Str),
			T_Fore,
			T_Back,
			Changes_Made,
			Type,
			Start_Line,
			Search_Amount,
			menu = menu_create,
			address_menu = menu_create
			;

	Set_Global_Int("@ADDRESS_MENU",address_menu);
	if (Menu_X < 1) {
		Menu_X = 1;
	}
	if (Menu_Y < 9) {
		Menu_Y = 9;
	}

	RM("COM_MSG_FKEY_ON");
	Type = Parse_Int('/TP=',MParm_Str);
	X_Str = Str(Menu_X);
	Y_Str = Str(Menu_Y + Type - 8);

	Changes_Made = Global_Int('COM_CHANGES_MADE');

	if (Type == 1) {
		Call SETUP_GENERAL;
	}
	if (Type == 2) {
		Call SETUP_MODEM;
	}
	if (Type == 3) {
		Call SETUP_TERMINAL;
	}
	if (Type == 4) {
		Call SETUP_PORT;
	}
	if (Type == 5) {
		Call SETUP_SCREEN;
	}
	if ((Type == 6) | (Type == 7)) {
		Direction = Copy('DOWNLOADUPLOAD',((Type - 6) * 8) + 1,8);
		Call SETUP_PROTOCOL;
	}
	if (Type == 8) {
		Call SETUP_ASCII;
	}
	if (Type == 10) {
		Call SAVE_SETUP;
	}
	Goto EXIT;
/*********************************** SUBROUTINES *******************************/
SETUP_GENERAL:
	G_Str = Global_Str('Com_General_Params');
	menu_set_item(menu,1,'Download path                  ',Parse_Str('/DP=',G_Str),'/C=1/W=40/QK=1',0,0,0);
	menu_set_item(menu,2,'Upload path                    ',Parse_Str('/UP=',G_Str),'/C=1/W=40/QK=1',0,0,0);
	menu_set_item(menu,3,'Log file                       ',Parse_Str('/LF=',G_Str),'/C=1/W=40/ML=80/QK=1',0,0,0);
	menu_set_item(menu,4,'Beep on downloads and uploads  ','/T=On/F=Off','/C=1/W=3/QK=1',5,Parse_Int('/A=',G_Str),0);
	menu_set_item(menu,5,'Word wrap in file viewer       ','/T=On/F=Off','/C=1/W=3/QK=1',5,Parse_Int('/VWW=',G_Str),0);
	menu_set_item(menu,6,'Right margin for file viewer   ','','/C=1/W=4/QK=1',1,Parse_Int('/VRM=',G_Str),0);
	menu_set_item(menu,7,'Word wrap in message editor    ','/T=On/F=Off','/C=1/W=3/QK=2',5,Parse_Int('/MWW=',G_Str),0);
	menu_set_item(menu,8,'Right margin for message editor','','/C=1/W=4/QK=7',1,Parse_Int('/MRM=',G_Str),0);
	Return_int = menu;
	RM('USERIN^DATA_IN /HN=1/A=0/#=8/S=1/X=3/H=MECOM^SETGEN/T=GENERAL SETUP');
	if (Return_Int) {
		Changes_Made = Changes_Made | $01;
/* be sure trailing '\' is included on D/L and U/L path */
		if (menu_item_str(menu,1,2) != '') {
			if (Copy(menu_item_str(menu,1,2),Length(menu_item_str(menu,1,2)),1) != '\') {
				menu_set_item(menu,1,'',menu_item_str(menu,1,2) + '\','',0,0,0);
			}
		}
		if (menu_item_str(menu,2,2) != '') {
			if (Copy(menu_item_str(menu,2,2),Length(menu_item_str(menu,2,2)),1) != '\') {
				menu_set_item(menu,2,'',menu_item_str(menu,2,2) + '\','',0,0,0);
			}
		}
		Set_Global_Str('Com_General_Params','/GENERAL=' +
			'/DP=' + Caps(menu_item_str(menu,1,2)) +
			'/UP=' + Caps(menu_item_str(menu,2,2)) +
			'/LF=' + Caps(menu_item_str(menu,3,2)) +
			'/A=' + Str(menu_item_int(menu,4,2)) +
			'/VWW=' + Str(menu_item_int(menu,5,2)) +
			'/VRM=' + Str(menu_item_int(menu,6,2)) +
			'/MWW=' + Str(menu_item_int(menu,7,2)) +
			'/MRM=' + Str(menu_item_int(menu,8,2))
			);
	}
	Ret;

SETUP_MODEM:
	G_Str = Global_Str('Com_Modem_Params');
	menu_set_item(menu,1,"2 Second pause substitute character ",Parse_Str('/XP=',G_Str),"/C=1/W=1/QK=3",0,0,0);
	menu_set_item(menu,2,"Carriage return substitute character",Parse_Str('/XC=',G_Str),"/C=1/W=1/QK=1",0,0,0);
	menu_set_item(menu,3,"cOntrol substitute character        ",Parse_Str('/XT=',G_Str),"/C=1/W=1/QK=2",0,0,0);
	menu_set_item(menu,4,"Escape substitute character         ",Parse_Str('/XE=',G_Str),"/C=1/W=1/QK=1",0,0,0);
	menu_set_item(menu,5,"Modem initialization command        ",Parse_Str('/I=',G_Str),"/C=1/W=37/QK=1",0,0,0);
	menu_set_item(menu,6,"Dialing command prefix              ",Parse_Str('/DC=',G_Str),"/C=1/W=37/QK=1",0,0,0);
	menu_set_item(menu,7,"dIaling command suffix              ",Parse_Str('/DS=',G_Str),"/C=1/W=37/QK=2",0,0,0);
	menu_set_item(menu,8,"Hangup command                      ",Parse_Str('/H=',G_Str),"/C=1/W=37/QK=1",0,0,0);
	menu_set_item(menu,9,"hAngup method","","/C=1/W=19/QK=2",10,0,0);
	menu_set_item(menu,10,"Send hangup command","","/C=3/G=@HANG@/R=0",12,Parse_Int('/HM=',G_Str) == 0,0);
	menu_set_item(menu,11,"Drop DTR ","","/L=10/C=27/G=@HANG@/R=1",12,Parse_Int('/HM=',G_Str) != 0,0);
	menu_set_item(menu,12,"Redial timeout                      ","","/C=1/W=2/QK=1",1,Parse_Int('/RT=',G_Str),0);
	menu_set_item(menu,13,"redial Pause                        ","","/C=1/W=2/QK=8",1,Parse_Int('/RP=',G_Str),0);
	menu_set_item(menu,14,"modem coNnect response              ",Parse_Str('/CS=',G_Str),"/C=1/W=37/QK=9",0,0,0);
	menu_set_item(menu,15,"modem no connect response 1         ",Parse_Str('/NC1=',G_Str),"/C=1/W=37/QK=27",0,0,0);
	menu_set_item(menu,16,"modem no connect response 2         ",Parse_Str('/NC2=',G_Str),"/C=1/W=37/QK=27",0,0,0);
	menu_set_item(menu,17,"modem no connect response 3         ",Parse_Str('/NC3=',G_Str),"/C=1/W=37/QK=27",0,0,0);
	menu_set_item(menu,18,"modem no connect response 4         ",Parse_Str('/NC4=',G_Str),"/C=1/W=37/QK=27",0,0,0);

	Return_Int = menu;
	RM( 'USERIN^DATA_IN /HN=1/A=0/#=18/S=1/H=MECOM^SETMOD/T=MODEM SETUP');

	if (Return_Int) {
		Changes_Made = Changes_Made | $02;
		Set_Global_Str('Com_Modem_Params','/MODEM=' +
			'/XP=' + Caps(menu_item_str(menu,1,2) +
			'/XC=' + menu_item_str(menu,2,2) +
			'/XT=' + menu_item_str(menu,3,2) +
			'/XE=' + menu_item_str(menu,4,2) +
			'/I=' +  menu_item_str(menu,5,2) +
			'/DC=' + menu_item_str(menu,6,2) +
			'/DS=' + menu_item_str(menu,7,2) +
			'/H=' +  menu_item_str(menu,8,2) +
			'/HM=' + Str(Global_Int('@HANG@')) +
			'/RT=' + Str(menu_item_int(menu,12,2)) +
			'/RP=' + Str(menu_item_int(menu,13,2)) +
			'/CS=' +  menu_item_str(menu,14,2) +
			'/NC1=' + menu_item_str(menu,15,2) +
			'/NC2=' + menu_item_str(menu,16,2) +
			'/NC3=' + menu_item_str(menu,17,2) +
			'/NC4=' + menu_item_str(menu,18,2))
			);
	}
		Set_Global_Int('@HANG@',0);
	Ret;

SETUP_TERMINAL:
	G_Str = Global_Str('Com_Terminal_Params');

	menu_set_item(menu,1,"Terminal emulation","","/C=1/QK=1",10,0,0);
	menu_set_item(menu,2,"",Parse_Str('/T=',G_Str),"/C=3/QK=1/W=8/M=COM_TERM_LIST",8,0,0);
	Set_Global_Str("@TERM@","N=" + menu_item_str(menu,2,2) +
													"M=" + Global_Str("COM_TERMINAL_MACRO"));
	menu_set_item(menu,3,"Duplex","","/C=22/QK=1/L=1",10,0,0);
	menu_set_item(menu,4,"Full","","/C=23/G=@DUPLEX@/R=0/L=2",12,Parse_Int('/D=',G_Str) == 0,0);
	menu_set_item(menu,5,"Half ","","/C=32/G=@DUPLEX@/R=1/L=2",12,Parse_Int('/D=',G_Str) != 0,0);
	menu_set_item(menu,6,"Incoming CR=","","/C=1/QK=1/L=4",10,0,0);
	menu_set_item(menu,7,"Cr","","/C=2/G=@ICR@/R=0/L=5",12,Parse_Int('/CI=',G_Str) == 0,0);
	menu_set_item(menu,8,"Cr-Lf ","","/C=9/G=@ICR@/R=1/L=5",12,Parse_Int('/CI=',G_Str) != 0,0);
	menu_set_item(menu,9,"Outgoing CR=","","/C=22/QK=1/L=4",10,0,0);
	menu_set_item(menu,10,"Cr","","/C=23/G=@OCR@/R=0/L=5",12,Parse_Int('/CO=',G_Str) == 0,0);
	menu_set_item(menu,11,"Cr-Lf ","","/C=30/G=@OCR@/R=1/L=5",12,Parse_Int('/CO=',G_Str) != 0,0);
	menu_set_item(menu,12,"Backspace","","/C=1/QK=1/L=7",10,0,0);
	menu_set_item(menu,13,"Non-destructive","","/C=2/G=@BS@/R=0/L=8",12,Parse_Int('/BT=',G_Str) == 0,0);
	menu_set_item(menu,14,"Destructive ","","/C=22/G=@BS@/R=1/L=8",12,Parse_Int('/BT=',G_Str) != 0,0);
	menu_set_item(menu,15,"ASCII code of outgoing BS=      ","","/C=3/W=3/QK=1/L=10",1,Parse_Int('/BK=',G_Str),0);
	menu_set_item(menu,16,"Line wrap                       ",'/T=On/F=Off',"/C=3/W=3/QK=1",5,Parse_Int('/W=',G_Str),0);
	menu_set_item(menu,17,"bReak duration in clock ticks=  ","","/C=3/W=2/QK=2",1,Parse_Int('/BL=',G_Str),0);
	menu_set_item(menu,18,"iGnore color attribute commands=",'/T=On/F=Off',"/C=3/W=3/QK=2",5,Parse_Int('/NC=',G_Str),0);

	Return_Int = menu;
	RM( 'USERIN^DATA_IN /HN=1/A=0/#=18/S=1/H=MECOM^SETTER/T=TERMINAL SETUP');

	if (Return_Int) {
		Changes_Made = Changes_Made | $04;
		Set_Global_Str('Com_Terminal_Params','/TERMINAL=' +
			'/T=' + Parse_Str("N=",Global_Str("@TERM@")) +
			'/D=' + Str(Global_Int('@DUPLEX@')) +
			'/CI=' + Str(Global_Int('@ICR@')) +
			'/CO=' + Str(Global_Int('@OCR@')) +
			'/BT=' + Str(Global_Int('@BS@')) +
			'/BK=' + Str(menu_item_int(menu,15,2)) +
			'/W=' +  Str(menu_item_int(menu,16,2)) +
			'/BL=' + Str(menu_item_int(menu,17,2)) +
			'/NC=' + Str(menu_item_int(menu,18,2))
			);
		Set_Global_Int('COM_DUPLEX',Global_Int('@DUPLEX@'));
	}

	Set_Global_Str("@TERM@","");
	Set_Global_Int("@DUPLEX@",0);
	Set_Global_Int("@ICR@",0);
	Set_Global_Int("@OCR@",0);
	Set_Global_Int("@BS@",0);
	Ret;

SETUP_PORT:
	G_Str = Global_Str('Com_Line_Params');

	menu_set_item(menu,1,"active Com port:","","/L=1/C=1/QK=8",10,0,0);
	Mstr = '/G=@PORT@/L=2';
	Jx = Parse_Int('/C=',G_Str);
	menu_set_item(menu,2,"1 ","",MStr + "/R=1/C=2",12, Jx < 2,0);
	menu_set_item(menu,3,"2 ","",MStr + "/R=2/C=9",12, Jx == 2,0);
	menu_set_item(menu,4,"3 ","",MStr + "/R=3/C=16",12, Jx == 3,0);
	menu_set_item(menu,5,"4 ","",MStr + "/R=4/C=23",12, Jx == 4,0);
	menu_set_item(menu,6,"5 ","",MStr + "/R=5/C=30",12, Jx == 5,0);
	menu_set_item(menu,7,"6 ","",MStr + "/R=6/C=37",12, Jx == 6,0);
	menu_set_item(menu,8,"7 ","",MStr + "/R=7/C=44",12, Jx == 7,0);
	menu_set_item(menu,9,"8 ","",MStr + "/R=8/C=51",12, Jx == 8,0);
	menu_set_item(menu,10,"Baud rate:","","/L=4/C=1/QK=1",10,0,0);
	Mstr = '/G=@BAUD@/C=2';
	Jx = Parse_Int('/B=',G_Str);
	menu_set_item(menu,11,"110   ","",MStr + "/R=1",12, Jx < 1,0);
	menu_set_item(menu,12,"300   ","",MStr + "/R=3",12, Jx == 2,0);
	menu_set_item(menu,13,"600   ","",MStr + "/R=4",12, Jx == 3,0);
	menu_set_item(menu,14,"1200  ","",MStr + "/R=5",12, Jx == 4,0);
	menu_set_item(menu,15,"2400  ","",MStr + "/R=6",12, Jx == 5,0);
	menu_set_item(menu,16,"4800  ","",MStr + "/R=7",12, Jx == 6,0);
	menu_set_item(menu,17,"9600  ","",MStr + "/R=8",12, Jx == 7,0);
	menu_set_item(menu,18,"19200 ","",MStr + "/R=9",12, Jx == 8,0);
/* Notice 38400 is out of sequence because it uses a value once used for 150 */
	menu_set_item(menu,19,"38400 ","",MStr + "/R=2",12, Jx == 1,0);
	menu_set_item(menu,20,"Parity:","","/L=4/C=22/QK=1",10,0,0);
	MStr = '/G=@PARITY@/C=23';
	Jx = Parse_Int('/P=',G_Str);
	menu_set_item(menu,21,"None ","",Mstr + "/R=1/L=5",12, Jx < 1,0);
	menu_set_item(menu,22,"Odd  ","",Mstr + "/R=2/L=6",12, Jx == 1,0);
	menu_set_item(menu,23,"Even ","",Mstr + "/R=3/L=7",12, Jx == 2,0);
	menu_set_item(menu,24,"sTop bits:","","/L=4/C=41/QK=2",10,0,0);
	Mstr = '/G=@STOP@/C=42/QK=1';
	Jx = Parse_Int('/S=',G_Str);
	menu_set_item(menu,25,"1 ","",Mstr + "/R=1/L=5",12, Jx != 1,0);
	menu_set_item(menu,26,"2 ","",Mstr + "/R=2/L=6",12, Jx == 1,0);
	menu_set_item(menu,27,"Data bits:","","/L=9/C=22/QK=1",10,0,0);
	MStr = '/G=@DATA@/C=23';
	Jx = Parse_Int('/D=',G_Str);
	menu_set_item(menu,28,"5 ","",Mstr + "/R=1/L=10",12, Jx < 0,0);
	menu_set_item(menu,29,"6 ","",Mstr + "/R=2/L=11",12, Jx == 1,0);
	menu_set_item(menu,30,"7 ","",Mstr + "/R=3/L=12",12, Jx == 2,0);
	menu_set_item(menu,31,"8 ","",Mstr + "/R=4/L=13",12, Jx == 3,0);
	menu_set_item(menu,32,"Handshaking:","","/L=9/C=41/QK=1",10,0,0);
	Call INIT_33_37;
	menu_set_item(menu,38,"Setup port addresses, interrupts, and types","","/QK=1/R=10/L=17/C=8",11,0,0);

/* We must initialize the addresses, etc even if we aren't using them */
	RM('COM_SETUP_PORT /I=1');

PORT_LOOP:
	Return_Int = menu;
	RM( 'USERIN^DATA_IN /HN=1/A=0/#=38/S=1/T=PORT SETUP/H=MECOM^SETPOR');

	if (Return_Int == 10) {
		RM('COM_SETUP_PORT');
		Goto PORT_LOOP;
	}

	if (Return_Int) {
ADDRESS_ONLY:
		Jx = Parse_Int('/C=',Global_Str('Com_Line_Params'));
		Changes_Made = Changes_Made | $08;
		Set_Global_Str('Com_Line_Params','/LINE=' +
			'/C=' + Str(Global_Int('@PORT@')) +
			'/B=' + Str(Global_Int('@BAUD@') - 1) +
			'/P=' + Str(Global_Int('@PARITY@') - 1) +
			'/D=' + Str(Global_Int('@DATA@') - 1) +
			'/S=' + Str(Global_Int('@STOP@') - 1) +
			'/IB=' + Str(menu_item_int(menu,36,2)) +
			'/OB=' + Str(menu_item_int(menu,37,2)) +
			'/XON=' +	Str(menu_item_int(menu,33,2)) +
			'/RTS=' +	Str(menu_item_int(menu,34,2)) +
			'/DTR=' +	Str(menu_item_int(menu,35,2)) +

			'/A1=' + Str(menu_item_int(address_menu,1,2)) +
			'/I1=' + Str(menu_item_int(address_menu,2,2)) +
			'/T1=' + Str(menu_item_int(address_menu,3,2)) +
			'/A2=' + Str(menu_item_int(address_menu,4,2)) +
			'/I2=' + Str(menu_item_int(address_menu,5,2)) +
			'/T2=' + Str(menu_item_int(address_menu,6,2)) +
			'/A3=' + Str(menu_item_int(address_menu,7,2)) +
			'/I3=' + Str(menu_item_int(address_menu,8,2)) +
			'/T3=' + Str(menu_item_int(address_menu,9,2)) +
			'/A4=' + Str(menu_item_int(address_menu,10,2)) +
			'/I4=' + Str(menu_item_int(address_menu,11,2)) +
			'/T4=' + Str(menu_item_int(address_menu,12,2)) +
			'/A5=' + Str(menu_item_int(address_menu,13,2)) +
			'/I5=' + Str(menu_item_int(address_menu,14,2)) +
			'/T5=' + Str(menu_item_int(address_menu,15,2)) +
			'/A6=' + Str(menu_item_int(address_menu,16,2)) +
			'/I6=' + Str(menu_item_int(address_menu,17,2)) +
			'/T6=' + Str(menu_item_int(address_menu,18,2)) +
			'/A7=' + Str(menu_item_int(address_menu,19,2)) +
			'/I7=' + Str(menu_item_int(address_menu,20,2)) +
			'/T7=' + Str(menu_item_int(address_menu,21,2)) +
			'/A8=' + Str(menu_item_int(address_menu,22,2)) +
			'/I8=' + Str(menu_item_int(address_menu,23,2)) +
			'/T8=' + Str(menu_item_int(address_menu,24,2))
		);
		RM('MECOM^COM_OPEN_PORT /P=' + Str(Jx));
		Set_Global_Int('@PORT@',0);
		Set_Global_Int('@BAUD@',0);
		Set_Global_Int('@PARITY@',0);
		Set_Global_Int('@DATA@',0);
		Set_Global_Int('@STOP@',0);
	} else if (Global_Int('@PORT_RETURN')) {
/* If they cancelled the main port screen, but accepted the address screen,
handle that. */
		Set_Global_Int('@PORT@',Parse_Int('/C=',G_Str));
		Set_Global_Int('@BAUD@',Parse_Int('/B=',G_Str) + 1);
		Set_Global_Int('@PARITY@',Parse_Int('/P=',G_Str) + 1);
		Set_Global_Int('@STOP@',Parse_Int('/S=',G_Str) + 1);
		Set_Global_Int('@DATA@',Parse_Int('/D=',G_Str) + 1);
		Call INIT_33_37;
		Goto ADDRESS_ONLY;
	}

	Set_Global_Str('A_COM_LINE_PARAMS','');
	Set_Global_Int('@PORT_RETURN',0);

	RET;

INIT_33_37:
	Mstr = "/C=42/L=1";
	menu_set_item(menu,33,"Xon-xoff","",Mstr + "0",13,Parse_Int('/XON=',G_Str),0);
	menu_set_item(menu,34,"CTS-RTS ","",Mstr + "1",13,Parse_Int('/RTS=',G_Str),0);
	menu_set_item(menu,35,"DSR-DTR ","",Mstr + "2",13,Parse_Int('/DTR=',G_Str),0);
	menu_set_item(menu,36,"Input buffer size","","/W=4/C=7/L=15/QK=1",1,Parse_Int('/IB=',G_Str),0);
	menu_set_item(menu,37,"Output buffer size","","/W=4/C=32/L=15/QK=1",1,Parse_Int('/OB=',G_Str),0);
	RET;

SETUP_SCREEN:
	Sub_Choice = 1;
	B_Str = '';

SCREEN_MENU:
	menu_set_item(menu,1,'Colors',"",'/H=MECOM^SETSCRCOL',0,0,0);
	menu_set_item(menu,2,'Box size',"",'/H=MECOM^SETSCRBOX',0,0,0);
	menu_set_item(menu,3,
'Full screen ^' + Copy('OffOn ',(Parse_Int('/FS=',Global_Str('Com_Screen_Params')) * 3) + 1,3)
	,"",'/H=MECOM^SETSCRSCR',0,0,0);

	Return_Int = menu;
	RM('USERIN^SUBMENU /HN=1/#=3/L=SETUP SCREEN/S=' + Str(Sub_Choice));

	Sub_Choice = Return_Int;
	if (Sub_Choice > 0) {
		B_Str = '/B=1/BO=1';
		if (Sub_Choice == 1) {
COLOR_MENU:
			Menu_X = 4;
			RETURN_INT = (Parse_Int('/F=',Global_Str('Com_Color_Params')) & $0F) +
									(Parse_Int('/B=',Global_Str('Com_Color_Params')) << 4 );
			RM('SETUP^COLORCHART');
			if (return_int != -1) {
				Changes_Made = Changes_Made | $10;
				Set_Global_Str('Com_Color_Params','/COLOR=' +
					'/F=' + Str(RETURN_INT & $0F) +
					'/B=' + Str((RETURN_INT >> 4) & $0F)
					);

			}
		}
		if (Sub_Choice == 2) {
			Message_Row = 1;
			Save_Box(1,1,80,Screen_Length);
			G_Str = Global_Str('Com_Box_Params');
			RM('WINDOW^MOVE_WIN /K=1/MX1=0/MY1=0' +
			'/MX2=' + Str(Screen_Width + 1) +
				'/MY2=' + Str(Screen_Length - 1) +
				'/X1=' + Parse_Str('/X1=',G_Str) +
				'/Y1=' + Parse_Str('/Y1=',G_Str) +
				'/X2=' + Parse_Str('/X2=',G_Str) +
				'/Y2=' + Parse_Str('/Y2=',G_Str));
			Kill_Box;
			Changes_Made = Changes_Made | $10;
			Set_Global_Str('Com_Box_Params','/BOX=' +
				'/X1=' + Parse_Str('/X1=',Return_Str) +
				'/Y1=' + Parse_Str('/Y1=',Return_Str) +
				'/X2=' + Parse_Str('/X2=',Return_Str) +
				'/Y2=' + Parse_Str('/Y2=',Return_Str)
				);
			Message_Row = 0;
		}
		if (Sub_Choice == 3) {
			Set_Global_Str('Com_Screen_Params','/SCREEN=/FS=' +
				Str(Not(Parse_Int('/FS=',Global_Str('Com_Screen_Params'))))
				);
			Changes_Made = Changes_Made | $10;
		}
		Goto SCREEN_MENU;
	}
	if (B_Str != '') {
		Kill_Box;
	}

	RET;

SETUP_PROTOCOL:
	Sub_Choice = Type - 6;
	RM('MECOM^COM_PROTO_MENU /LO=0/X=3/Y=' + Str(Menu_Y - 4 + Sub_Choice) + '/DU=' + Str(Sub_Choice));
	RET;

SETUP_ASCII:
	G_Str = Global_Str('Com_Ascii_Params');

	menu_set_item(menu,1,"local Echo                        ","","/C=1/QK=7",13,Parse_Int('/E=',G_Str),0);
	menu_set_item(menu,2,"add space character to Blank lines","","/C=1/QK=24",13,Parse_Int('/S=',G_Str),0);
	menu_set_item(menu,3,"Ascii code of pace character      ","","/C=1/W=3/QK=1/L=4",1,Parse_Int('/PC=',G_Str),0);
	menu_set_item(menu,4,"Character pacing(in milliseconds) ","","/C=1/W=2/QK=1",1,Parse_Int('/CP=',G_Str),0);
	menu_set_item(menu,5,"Line pacing(in 100ths of a second)","","/C=1/W=2/QK=1",1,Parse_Int('/LP=',G_Str),0);
	menu_set_item(menu,6,"Upload CR=","","/C=6/QK=1/L=8",10,0,0);
	menu_set_item(menu,7,"Cr   ","","/C=7/G=@UCR@/R=0",12,Parse_Int('/CU=',G_Str) == 0,0);
	menu_set_item(menu,8,"Cr-Lf","","/C=7/G=@UCR@/R=1",12,Parse_Int('/CU=',G_Str) == 1,0);
	menu_set_item(menu,9,"Strip","","/C=7/G=@UCR@/R=2",12,Parse_Int('/CU=',G_Str) == 2,0);
	menu_set_item(menu,10,"uPload LF=","","/C=23/QK=2/L=8",10,0,0);
	menu_set_item(menu,11,"Lf   ","","/C=24/G=@ULF@/R=0/L=9",12,Parse_Int('/LU=',G_Str) == 0,0);
	menu_set_item(menu,12,"Cr-Lf","","/C=24/G=@ULF@/R=1/L=10",12,Parse_Int('/LU=',G_Str) == 1,0);
	menu_set_item(menu,13,"Strip","","/C=24/G=@ULF@/R=2/L=11",12,Parse_Int('/LU=',G_Str) == 2,0);
	menu_set_item(menu,14,"Download CR=","","/C=6/QK=1/L=13",10,0,0);
	menu_set_item(menu,15,"Cr   ","","/C=7/G=@DCR@/R=0/L=14",12,Parse_Int('/CD=',G_Str) == 0,0);
	menu_set_item(menu,16,"Cr-Lf","","/C=7/G=@DCR@/R=1/L=15",12,Parse_Int('/CD=',G_Str) == 1,0);
	menu_set_item(menu,17,"Strip","","/C=7/G=@DCR@/R=2/L=16",12,Parse_Int('/CD=',G_Str) == 2,0);
	menu_set_item(menu,18,"dOwnload LF=","","/C=23/QK=2/L=13",10,0,0);
	menu_set_item(menu,19,"Lf   ","","/C=24/G=@DLF@/R=0/L=14",12,Parse_Int('/LD=',G_Str) == 0,0);
	menu_set_item(menu,20,"Cr-Lf","","/C=24/G=@DLF@/R=1/L=15",12,Parse_Int('/LD=',G_Str) == 1,0);
	menu_set_item(menu,21,"Strip","","/C=24/G=@DLF@/R=2/L=16",12,Parse_Int('/LD=',G_Str) == 2,0);

	Return_Int = menu;
	RM( 'USERIN^DATA_IN /HN=1/A=0/#=21/S=1/H=MECOM^SETASC/T=ASCII TRANSFER SETUP');

	if (Return_Int) {
		Changes_Made = Changes_Made | $20;
		Set_Global_Str('Com_Ascii_Params','/ASCII=' +
			'/E=' +  Str(menu_item_int(menu,1,2)) +
			'/S=' +  Str(menu_item_int(menu,2,2)) +
			'/PC=' + Str(menu_item_int(menu,3,2)) +
			'/CP=' + Str(menu_item_int(menu,4,2)) +
			'/LP=' + Str(menu_item_int(menu,5,2)) +
			'/CU=' + Str(Global_Int('@UCR@')) +
			'/LU=' + Str(Global_Int('@ULF@')) +
			'/CD=' + Str(Global_Int('@DCR@')) +
			'/LD=' + Str(Global_Int('@DLF@'))
			);
	}

	Set_Global_Int('@UCR@',0);
	Set_Global_Int('@ULF@',0);
	Set_Global_Int('@DCR@',0);
	Set_Global_Int('@DLF@',0);
	RET;

SAVE_SETUP:
/* Look for INITCOM.ME in MECOM.DB */
	RM('Setconfig /DB=MECOM/T=INITCOM.ME');
	if (Return_Int == 0) {
/* If we can't find it, create it */
		RM('COM_INIT');
	}
/* Save the beginning position */
	Start_Line = C_Line;
	Eol;
/* Find the next page title or EOF */
	if (Search_Fwd('|12',0) == 0) {
		Eof;
		if (C_Col > 1) {
			Down;
		}
	}
	Search_Amount = C_Line - Start_Line;

	G_Str = 'General';
	Call UPDATE_LINE;

	G_Str = 'Box';
	Call UPDATE_LINE;

	G_Str = 'Color';
	Call UPDATE_LINE;

	G_Str = 'Screen';
	Call UPDATE_LINE;

	G_Str = 'Modem';
	Call UPDATE_LINE;

	G_Str = 'Terminal';
	Call UPDATE_LINE;

	G_Str = 'Line';
	Call UPDATE_LINE;

	G_Str = 'ASCII';
	Call UPDATE_LINE;
	Save_File;
	Update_Status_Line;
	RET;

UPDATE_LINE:
	Goto_Line(Start_Line);
	if (Search_Fwd('/' + G_Str + '=',Search_Amount) ==	False) {
		Eol;
		++Search_Amount;
		Insert_Mode = True;
		Cr;
	}
	Put_Line(Global_Str('COM_' + G_Str + '_PARAMS'));
	RET;

/*******************************************************************************/

EXIT:
	Set_Global_Int('COM_CHANGES_MADE',Changes_Made);
	if (Type == 10) {
		Return_Int = 100;
	} else {
		Return_Int = 0;
	}

	Menu_Delete(menu);
	Menu_Delete(address_menu);
	Set_Global_Int("@ADDRESS_MENU",0);

	RM("COM_MSG_FKEY_OFF");
}

macro COM_TERM_LIST {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_TERM_LIST

Description:	Pops up a list of terminal types for COM_SETUP.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	RM("USERIN^DB /NDF=1/F=MECOM/DPT=TERMINALS.DB/PRE=TT/GLO=@TERM@/NE=0/NC=1/LO=1/U=1/LT=TERMINAL EMULATION/H=/MACRO=COM_TERM_WARNING");
}

macro COM_TERM_WARNING {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  COM_TERM_WARNING

Description:  Warns user that he cannot just arbitrarily add terminal types
							without having support for them.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	if (Parse_Int('/P=',MParm_Str)) {
		RM("MEERROR^MessageBox /T=WARNING/B=1/M=If you added any new terminal types, be sure that you have the appropriate support macro(s).");
	}
}

macro COM_SETUP_PORT {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_SETUP_PORT

Description:	This macro is the port address, interrupt, and type menu called
from COM_SETUP.  This macro is not intended to be called from anything but
COM_SETUP.

Parameters:
							/I= If 1, initialize the globals only, don't show the menu.
							/X=, /Y=  The upper left hand X and Y coordinates.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int menu = Global_Int("@ADDRESS_MENU");

	if (menu == 0) {
		Goto EXIT;
	}

	if (Parse_Int('/I=',MParm_Str)) {
		Return_Str = Global_Str('COM_LINE_PARAMS');
	} else {
		Return_Str = Global_Str('A_COM_LINE_PARAMS');
	}

	menu_set_item(menu,1,"COM1 address","","/C=1/L=1/W=4",4,Parse_Int('/A1=',Return_Str),0);
	menu_set_item(menu,2,"COM1 interrupt","","/C=20/L=1/W=3",1,Parse_Int('/I1=',Return_Str),0);
	menu_set_item(menu,3,"COM1 connection",'/T=Modem/F=Direct',"/C=39/L=1/W=6",5,Parse_Int('/T1=',Return_Str),0);
	menu_set_item(menu,4,"COM2 address","","/C=1/L=2/W=4",4,Parse_Int('/A2=',Return_Str),0);
	menu_set_item(menu,5,"COM2 interrupt","","/C=20/L=2/W=3",1,Parse_Int('/I2=',Return_Str),0);
	menu_set_item(menu,6,"COM2 connection",'/T=Modem/F=Direct',"/C=39/L=2/W=6",5,Parse_Int('/T2=',Return_Str),0);
	menu_set_item(menu,7,"COM3 address","","/C=1/L=3/W=4",4,Parse_Int('/A3=',Return_Str),0);
	menu_set_item(menu,8,"COM3 interrupt","","/C=20/L=3/W=3",1,Parse_Int('/I3=',Return_Str),0);
	menu_set_item(menu,9,"COM3 connection",'/T=Modem/F=Direct',"/C=39/L=3/W=6",5,Parse_Int('/T3=',Return_Str),0);
	menu_set_item(menu,10,"COM4 address","","/C=1/L=4/W=4",4,Parse_Int('/A4=',Return_Str),0);
	menu_set_item(menu,11,"COM4 interrupt","","/C=20/L=4/W=3",1,Parse_Int('/I4=',Return_Str),0);
	menu_set_item(menu,12,"COM4 connection",'/T=Modem/F=Direct',"/C=39/L=4/W=6",5,Parse_Int('/T4=',Return_Str),0);
	menu_set_item(menu,13,"COM5 address","","/C=1/L=5/W=4",4,Parse_Int('/A5=',Return_Str),0);
	menu_set_item(menu,14,"COM5 interrupt","","/C=20/L=5/W=3",1,Parse_Int('/I5=',Return_Str),0);
	menu_set_item(menu,15,"COM5 connection",'/T=Modem/F=Direct',"/C=39/L=5/W=6",5,Parse_Int('/T5=',Return_Str),0);
	menu_set_item(menu,16,"COM6 address","","/C=1/L=6/W=4",4,Parse_Int('/A6=',Return_Str),0);
	menu_set_item(menu,17,"COM6 interrupt","","/C=20/L=6/W=3",1,Parse_Int('/I6=',Return_Str),0);
	menu_set_item(menu,18,"COM6 connection",'/T=Modem/F=Direct',"/C=39/L=6/W=6",5,Parse_Int('/T6=',Return_Str),0);
	menu_set_item(menu,19,"COM7 address","","/C=1/L=7/W=4",4,Parse_Int('/A7=',Return_Str),0);
	menu_set_item(menu,20,"COM7 interrupt","","/C=20/L=7/W=3",1,Parse_Int('/I7=',Return_Str),0);
	menu_set_item(menu,21,"COM7 connection",'/T=Modem/F=Direct',"/C=39/L=7/W=6",5,Parse_Int('/T7=',Return_Str),0);
	menu_set_item(menu,22,"COM8 address","","/C=1/L=8/W=4",4,Parse_Int('/A8=',Return_Str),0);
	menu_set_item(menu,23,"COM8 interrupt","","/C=20/L=8/W=3",1,Parse_Int('/I8=',Return_Str),0);
	menu_set_item(menu,24,"COM8 connection",'/T=Modem/F=Direct',"/C=39/L=8/W=6",5,Parse_Int('/T7=',Return_Str),0);

/* If we simply want to initialize the menu_items, then don't do the DATA_IN */
	if (Parse_Int('/I=',MParm_Str)) {
		Set_Global_Str('A_COM_LINE_PARAMS',Return_Str);
		Goto EXIT;
	}

	Return_Int = menu;
	RM('DATA_IN /HN=1/PRE=A/#=24/T=PORT SETUP/H=MECOM^SETPOR');

	Set_Global_Int('@PORT_RETURN',Return_Int);
	if (Return_Int) {
		Set_Global_Str('A_Com_Line_Params',
			'/A1=' + Str(menu_item_int(menu,1,2)) +
			'/I1=' + Str(menu_item_int(menu,2,2)) +
			'/T1=' + Str(menu_item_int(menu,3,2)) +
			'/A2=' + Str(menu_item_int(menu,4,2)) +
			'/I2=' + Str(menu_item_int(menu,5,2)) +
			'/T2=' + Str(menu_item_int(menu,6,2)) +
			'/A3=' + Str(menu_item_int(menu,7,2)) +
			'/I3=' + Str(menu_item_int(menu,8,2)) +
			'/T3=' + Str(menu_item_int(menu,9,2)) +
			'/A4=' + Str(menu_item_int(menu,10,2)) +
			'/I4=' + Str(menu_item_int(menu,11,2)) +
			'/T4=' + Str(menu_item_int(menu,12,2)) +
			'/A5=' + Str(menu_item_int(menu,13,2)) +
			'/I5=' + Str(menu_item_int(menu,14,2)) +
			'/T5=' + Str(menu_item_int(menu,15,2)) +
			'/A6=' + Str(menu_item_int(menu,16,2)) +
			'/I6=' + Str(menu_item_int(menu,17,2)) +
			'/T6=' + Str(menu_item_int(menu,18,2)) +
			'/A7=' + Str(menu_item_int(menu,19,2)) +
			'/I7=' + Str(menu_item_int(menu,20,2)) +
			'/T7=' + Str(menu_item_int(menu,21,2)) +
			'/A8=' + Str(menu_item_int(menu,22,2)) +
			'/I8=' + Str(menu_item_int(menu,23,2)) +
			'/T8=' + Str(menu_item_int(menu,24,2))
			);
	}
EXIT:
}

macro COM_PHONE {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_PHONE

Description:	The comunications module phone dialing and hang up macro.

Parameters:
							/MY= The Y coordinate reference point for making boxes.
							/TP=
										1   autodial menu
										2   manual dial menu
										3   hangup
										4   redial menu

Returns:
							Return_Int (only meaningful for redial)
								1 = successful
								0 = not successful

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
/*
This macro is the phone menu including the phone dialing list for the
communications module
/N=/#=/B=/P=/D=/S=/E=/M=
Parameters:
/TP=	1   Go directly to autodial menu
/TP=	2   Go directly to manual dial menu
/TP=	3   Go directly to hangup
/TP=	4   Go directly to redial menu
 */
	int Active_Window,DIAL_Window,Jx,Jy,Temp_Reg_Exp_Stat,Temp_Integer,
					Menu_Index,Temp_Insert_Mode,Menu_Choice,Error,Port_Num,Redial_Count,
					Menu_Y,Type,Response_Length,Terminator_Index,T_Start,T_Stop;
	str Temp_String2;
	str Temp_String,Modem_Response,Macro,Terminator_Chars,MStr;

	port_num = global_int('COM_PORT_NUM');

/* determine how much is the longest expected connect/no connect modem response */
	Response_Length = Length(Parse_Str('/CS=',Global_Str('Com_Modem_Params')));
	Jx = 0;
	while (Jx < 5) {
		++Jx;
		Temp_Integer = Length(Parse_Str('/NC' + Str(Jx) + '=',Global_Str('Com_Modem_Params')));
		if (Temp_Integer > Response_Length) {
			Response_Length = Temp_Integer;
		}
	}

	if (Response_Length == 0) {
		Response_Length = 10;
	}
	Macro = '';
	Menu_Y = Parse_Int('/MY=',MParm_Str);
	if (Menu_Y == 0) {
		Menu_Y = 2;
	}
	Menu_Index = 0;
	DIAL_Window = 0;
	Temp_Reg_Exp_Stat = Reg_Exp_Stat;
	Reg_Exp_Stat = False;
	Temp_Insert_Mode = Insert_Mode;
	Insert_Mode = True;
	Active_Window = Cur_Window;
/* Look for special parameters */
	Type = Parse_Int('/TP=',MParm_Str);
	if (Type == 1) {
		Goto AUTODIAL;
	}
	if (Type == 2) {
		Goto MANUALDIAL;
	}
	if (Type == 3) {
		Return_Int = True;
		Call HANG_UP;
		Goto EXIT;
	}
	if (Type == 4) {
		Goto DO_REDIAL;
	}

Goto EXIT;

	if (Menu_Choice == 1) {
/* Auto dial menu */
AUTODIAL:
		Set_Global_Str('PHONDS','N=28#=14');
		RM('USERIN^DB /ABT=Dial/F=MECOM/GLO=PHONE/LO=2/PRE=PHON/NOALPHA=1/ENC=0/DPT=' +
			'PHONELST.DB/NDF=1/LT=Autodial Directory.  <ENTER> to dial./X=9/Y=' +
			Str(Menu_Y + 4) + '/DT=AUTODIAL SETUP/H=MECOM^PHONAUT/DS=PHONDS/2TOP=1/MACRO=COM_PROCESS_DIAL');
		if (RETURN_INT) {
			MStr = Global_Str('PHONE');
			Temp_String = Parse_Str('#=',MStr);
			Macro = Parse_Str('M=',MStr);
			if (Temp_String != '') {
/* Set up the com port to the setttings in the phone list */
				Jx = Parse_Int('B=',Mstr);
				if (Jx > 0) {
					--Jx;
				}
				Jy = Parse_Int('P=',Mstr);
				if (Jy > 0) {
					--Jy;
				}
				Temp_Integer = Parse_Int('D=',Mstr);
				if (Temp_Integer > 0) {
					--Temp_Integer;
				}

				error = set_com( port_num, 1,Jx);
				error = set_com( port_num, 2,Jy);
				error = set_com( port_num, 3,Temp_Integer);
				error = set_com( port_num, 4,Parse_Int('S=',MStr));

				Set_Global_Int('COM_DUPLEX',Parse_Int('E=',MStr));

MANUAL_DIAL:
				Return_Str = Parse_Str('/DC=',Global_Str('Com_Modem_Params')) + Temp_String;
				Call WRITE_TO_PORT;
				Return_Str = Parse_Str('/DS=',Global_Str('Com_Modem_Params'));
				Call WRITE_TO_PORT;
				Set_Global_Str('Com_Last_Dialed_#',Temp_String);
				Set_Global_Str('Com_Last_Dialed_Macro',Macro);
				if (Type == 1) {
					Set_Global_Int('COM_CHANGES_MADE',Global_Int('COM_CHANGES_MADE') | $08);
					Return_Str = '/WRITE=Dialing ' + Parse_Str('N=',Global_Str('PHONE')) +
						'|13|10';
				} else {
					Return_Str = '';
				}
				if (Macro != '') {
					Return_Str = Return_Str + '/PM=' + Macro;
				}
			}
			Goto EXIT;
		} else {
			Goto ESCAPE_EXIT;
		}
	}

	if (Menu_Choice == 2) {
MANUALDIAL:
/* Manual dialing */
		Return_Str = '';

		RM('USERIN^QUERYBOX /C=9/W=40/H=MECOM^PHONMAN/P=Number to dial:/T=MANUAL DIALING/L=' + Str(Menu_Y + 5));
		if ((Return_Int != 0) & (Return_Str != '')) {
			Temp_String = Return_Str;
			Macro = '';
			Goto MANUAL_DIAL;
		}
		Goto ESCAPE_EXIT;
	}

	if (Menu_Choice == 3) {
/* Hang up the phone */
		Return_Int = False;
		Call HANG_UP;
		Goto ESCAPE_EXIT;
	}

	if (Menu_Choice == 4) {
/* redial  */
DO_REDIAL:
		Redial_Count = 0;
		if (Global_Str('Com_Last_Dialed_#') == '') {
			Return_Str = '';
			RM('USERIN^QUERYBOX /C=9/W=40/P=Number to dial:/T=REDIAL/L=' + Str(Menu_Y + 7));
			if (Return_Int) {
				Set_Global_Str('Com_Last_Dialed_#',Return_Str);
				Set_Global_Str('Com_Last_Dialed_Macro','');
			} else {
				Goto ESCAPE_EXIT;
			}
		}
		Put_Box(9,Menu_Y + 7,60,Menu_Y + 14,0,M_B_Color,'AUTOMATIC REDIAL',True);
		Write('<F2>=change parameters.  <ESC>=abort.',13,Menu_Y + 13,0,M_S_Color);
		Redial_Count = 0;

REDIAL_CHANGE:
		Write('Number: ' + Global_Str('Com_Last_Dialed_#'),10,Menu_Y + 8,0,M_S_Color);
		Write('Number of tries: 0',10,Menu_Y + 9,0,M_S_Color);
		Write('Elapsed time: 0' +  '  seconds of: '  + Parse_Str('/RT=',
			Global_Str('Com_Modem_Params')) + ' ',10,Menu_Y + 10,0,M_S_Color);
		Write('Delay before redial time: 0' +  '  seconds of: '  + Parse_Str('/RP=',
			Global_Str('Com_Modem_Params')),10,Menu_Y + 11,0,M_S_Color);
		Write('Last modem response: ',10,Menu_Y + 12,0,M_S_Color);

REDIAL:
		Write('0 ',24,Menu_Y + 10,0,M_S_Color);
		Write(Str(Redial_Count),27,Menu_Y + 9,0,M_S_Color);
		Return_Str = Parse_Str('/DC=',Global_Str('Com_Modem_Params')) +
			Global_Str('Com_Last_Dialed_#');
		Call WRITE_TO_PORT;
		Return_Str = Parse_Str('/DS=',Global_Str('Com_Modem_Params'));
		Call WRITE_TO_PORT;
/* This is just to clear the incoming port buffer */
		Delay(100);
		Terminator_Chars = '';
		Return_Int = 255;
		Call GET_MODEM_RESPONSE;
		Temp_Integer = Val(T_Start,Copy(Time,7,2));

TIMEOUT_LOOP:
		Temp_Integer = Val(T_Stop,Copy(Time,7,2));
		if (T_Start > T_Stop) {
			T_Stop = T_Stop + 60;
		}
		if (read_com_till(port_num,1,'',Temp_String2,
			Terminator_Index) == 0) {
			Return_Int = Response_Length;
			Terminator_Chars = '';
			Delay(500);
			Call GET_MODEM_RESPONSE;

			Temp_Integer = XPos(Parse_Str('/CS=',Global_Str('Com_Modem_Params')),
				Modem_Response,1);

			if (Temp_Integer) {
/* Save any remaining stuff after the connect response into a global so that it
can be dealt with later if needed. */
				Set_Global_Str('Com_Leftovers',Copy(Modem_Response,Temp_Integer +
					Length(Parse_Str('/CS=',Global_Str('Com_Modem_Params'))) + 2,255));
				Return_Int = True;
				Kill_Box;
				if (Global_Str('Com_Last_Dialed_Macro') != '') {
					Return_Str = '/PM=' + Global_Str('Com_Last_Dialed_Macro');
/* We are adding a special parameter "/AC=" to the macro command to indicate to
the macro that we are Already Connected.  If there are already parameters in
this macro command, we will not put in the space delimter. */
					if (XPos(' ',Return_Str,1) == 0) {
						Return_Str = Return_Str + ' ';
					}
					Return_Str = Return_Str + '/AC=1';
				}
				Goto EXIT;
			} else {
/* Check if it is any of the defined modem no connect responses */
				Jx = 1;
CHECK_NO_CONNECT:
				Return_Str = Parse_Str('/NC' + Str(Jx) + '=',Global_Str('Com_Modem_Params'));
				if (Return_Str != '') {
					Temp_Integer = XPos(Return_Str,Modem_Response,1);
					if ((Temp_Integer == 0) & (Jx < 4)) {
						++Jx;
						Goto CHECK_NO_CONNECT;
					}
				}
/* Clean up the modem response so it can be displayed */
/* Remove any CR or LF */
REMOVE_CR:
				Jx = XPos('|13',Modem_Response,1);
				if (Jx) {
					Modem_Response = Str_Del(Modem_Response,Jx,1);
					Goto REMOVE_CR;
				}
REMOVE_LF:
				Jx = XPos('|10',Modem_Response,1);
				if (Jx) {
					Modem_Response = Str_Del(Modem_Response,Jx,1);
					Goto REMOVE_LF;
				}
				Modem_Response = Copy(Modem_Response + '                              ',1,22);
/* If we got one of the no connect modem responses, then hang up, otherwise
display, but ignore the modem response */
				if (Temp_Integer) {
					Goto REDIAL_HANG_UP;
				}
				Goto TIMEOUT_LOOP;
			}
		}
		Write(Str(T_Stop - T_Start),24,Menu_Y + 10,0,M_S_Color);
		if (Check_Key) {
			if (Key1 == 27) {
				Goto ABORT_REDIAL;
			}
			Jx = Inq_Key(Key1,Key2,TERM,MStr);
			if ((Jx == 1) & (MStr == 'MECOM^COM_HELP')) {
				Help('MECOM^PHONRED');
			}
			if ((Key1 == 0) & (Key2 == 60)) {
				jx = menu_create;
				menu_set_item(jx,1,'Redial timeout            ','','/C=1/W=2/QK=1',1,Parse_Int('/RT=',Global_Str('Com_Modem_Params')),0);
				menu_set_item(jx,2,'Redial pause              ','','/C=1/W=2/QK=8',1,Parse_Int('/RP=',Global_Str('Com_Modem_Params')),0);
				RM("COM_MSG_FKEY_ON");
				Return_Int = jx;
				RM('USERIN^DATA_IN /HN=1/#=2/S=1/X=10/H=MECOM^PHONRED/T=REDIAL PARAMS/Y=' + Str(Menu_Y + 13));
				RM("COM_MSG_FKEY_OFF");

				if (Return_Int) {
					Return_Str = '/RT=';
					RM('USERIN^CHNGPARM /G=Com_Modem_Params/P=' + Str(Menu_Item_Int(jx,1,2)));
					Return_Str = '/RP=';
					RM('CHNGPARM /G=Com_Modem_Params/P=' + Str(Menu_Item_Int(jx,2,2)));
				}
				menu_delete(jx);
				Goto REDIAL_CHANGE;
			}
		}

		if ((T_Stop - T_Start) >= Parse_Int('/RT=',Global_Str('Com_Modem_Params'))) {
			Modem_Response = 'TIMEOUT               ';
			Goto REDIAL_HANG_UP;
		}
		Goto TIMEOUT_LOOP;

REDIAL_HANG_UP:
		Write(Modem_Response,31,Menu_Y + 12,0,M_S_Color);
		Call HANG_UP;
/* This is just to clear the incoming port buffer */
		Return_Int = 255;
		Terminator_Chars = '';
		Call GET_MODEM_RESPONSE;
		Write('0 ',36,Menu_Y + 11,0,M_S_Color);
		Temp_Integer = Val(T_Start,Copy(Time,7,2));

DELAY_LOOP:
		Temp_Integer = Val(T_Stop,Copy(Time,7,2));
		if (T_Start > T_Stop) {
			T_Stop = T_Stop + 60;
		}
		Write(Str(T_Stop - T_Start),36,Menu_Y + 11,0,M_S_Color);
		if (Check_Key) {
			if (Key1 == 27) {
				Goto ABORT_REDIAL;
			}
		}
		if ((T_Stop - T_Start) >= Parse_Int('/RP=',Global_Str('Com_Modem_Params'))) {
			++Redial_Count;
			Goto REDIAL;
		}
		Goto DELAY_LOOP;
ABORT_REDIAL:
		Return_Str = '';
		Call HANG_UP;
		Kill_Box;
	}

	Goto EXIT;


/*********************************** SUBROUTINES *******************************/
HANG_UP:
	Put_Box(9,Menu_Y + 6,22,Menu_Y + 9,0,M_B_Color,'',True);
	Write('Hanging up',10,Menu_Y + 7,0,Working_Color);
	if (Parse_Int('/HM=',Global_Str('Com_Modem_Params'))) {
/* Drop DTR method */
		Error = set_com(port_num,13,2);
		Delay(200);
		error = set_com(port_num,13,3);
	} else {
/* Send modem string method */
		Return_Str = Parse_Str('/H=',Global_Str('Com_Modem_Params'));
		Call WRITE_TO_PORT;
	}
	Kill_Box;
	RET;

GET_MODEM_RESPONSE:
	Modem_Response = Temp_String2;

MORE_RESPONSE:
	Delay(50);
	Error = read_com_till(port_num,Return_Int * 2,Terminator_Chars,
		Temp_String2,Terminator_Index);
	Modem_Response = Modem_Response + Temp_String2;
	if (Error == 0) {
		if ((Terminator_Index == 0) & (Svl(Modem_Response) < Return_Int)) {
			Goto MORE_RESPONSE;
		}
	}
	RET;

WRITE_TO_PORT:
/* This routine checks for the presence of any of the special modem translate
characters and replaces them with the proper character for output to the modem
then outputs the string to the modem */
	RM('COM_MODEM_OUT');
	RET;

/*******************************************************************************/

ESCAPE_EXIT:
	Return_Int =  False;
	Goto SKIP_EXIT;

EXIT:
	Return_Int = True;
SKIP_EXIT:
	if (DIAL_Window) {
		Switch_Window(DIAL_Window);
		if (File_Changed) {
			Save_File;
			Update_Status_Line;
		}
		Delete_Window;
	}
	Switch_Window(Active_Window);
	Reg_Exp_Stat = Temp_Reg_Exp_Stat;
	Insert_Mode = Temp_Insert_Mode;
}

macro COM_PROCESS_DIAL {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_PROCESS_DIAL

Description:	Initializes default values for newly created phone number.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	if ((Parse_Int('/P=',MParm_Str) == 0) &
			(Parse_Int('/CREATE=',MParm_Str) > 0)) {
		str G_Name[20] = Parse_Str('/GLO=',MParm_Str);
		Set_Global_Str(G_Name,Global_Str(G_Name) +
						'B=' + Str(Parse_Int('/B=',Global_Str('COM_LINE_PARAMS')) + 1) +
						'P=' + Str(Parse_Int('/P=',Global_Str('COM_LINE_PARAMS')) + 1) +
						'D=' + Str(Parse_Int('/D=',Global_Str('COM_LINE_PARAMS')) + 1) +
						'S=' + Parse_Str('/S=',Global_Str('COM_LINE_PARAMS')) +
						'E=' + Parse_Str('/D=',Global_Str('COM_TERMINAL_PARAMS'))
						);
	}
}

macro COM_MODEM_OUT {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_MODEM_OUT

Description:	Processes a string for being sent to a modem, then sends it.

Parameters:
							Return_Str = The string to be processed and sent.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	char Temp_Char;
	int Bytes_Written,Index,Port_Num = Global_Int('COM_PORT_NUM');

/* This macro checks for the presence of any of the special modem translate
characters and replaces them with the proper character for output to the modem. */

/* Check for carriage return char */
	Temp_Char = Parse_Str('/XC=',Global_Str('Com_Modem_Params'));
	if (Temp_Char != '|0') {
CHECK_FOR_CR:
		Index = XPos(Temp_Char,Return_Str,1);
		if (Index) {
			Return_Str = Copy(Return_Str,1,Index - 1) + '|13' +
							Copy(Return_Str,Index + 1,254);
			Goto CHECK_FOR_CR;
		}
	}

/* Check for control character */
	Temp_Char = Parse_Str('/XT=',Global_Str('Com_Modem_Params'));
	if (Temp_Char != '|0') {
CHECK_FOR_CTRL:
		Index = XPos(Temp_Char,Return_Str,1);
		if (Index) {
			Return_Str = Copy(Return_Str,1,Index - 1) +
										Char(Ascii(Copy(Return_Str,Index + 1,1)) - 64) +
										Copy(Return_Str,Index + 2,254);
			Goto CHECK_FOR_CTRL;
		}
	}

/* Check for escape character */
	Temp_Char = Parse_Str('/XE=',Global_Str('Com_Modem_Params'));
	if (Temp_Char != '|0') {
CHECK_FOR_ESC:
		Index = XPos(Temp_Char,Return_Str,1);
		if (Index) {
			Return_Str = Copy(Return_Str,1,Index - 1) + '|27' +
										Copy(Return_Str,Index + 1,254);
			Goto CHECK_FOR_ESC;
		}
	}

/* Check for 2 second delay char */
	Temp_Char = Parse_Str('/XP=',Global_Str('Com_Modem_Params'));
	if (Temp_Char != '|0') {
CHECK_FOR_DELAY:
		Index = XPos(Temp_Char,Return_Str,1);
		if (Index) {
			Write_Com(port_num,Copy(Return_Str,1,Index - 1), Bytes_Written);
			Delay(2000);
			if (Length(Return_Str) > Index) {
				Return_Str = Copy(Return_Str,Index + 1,254);
				Goto CHECK_FOR_DELAY;
			} else {
				RET;
			}
		}
	}

	Write_Com(port_num,Return_Str,Bytes_Written);
}

macro COM_SEND_BREAK {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_SEND_BREAK

Description:	Invokes a break condition on the currently active com port.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Error;
	Error = Send_Break(Global_Int('COM_PORT_NUM'),
						Parse_Int('/BL=',Global_Str('COM_TERMINAL_PARAMS')));
	if (Error) {
		Error_Level = 6000 + Error;
		RM('MEERROR /EM=Error sending break signal.');
	}
}

macro COM_WRITE_L_A {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_WRITE_L_A

Description:	Handles writing to the log and ASCII download file.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str Log_Str[2048] = Global_Str('Com_Log_Str');
	int
					Bytes_Written,
					Error,
					Term_Pos,
					Cr_Mode = Parse_Int('/CD=',Global_Str('Com_Ascii_Params')),
					Lf_Mode = Parse_Int('/LD=',Global_Str('Com_Ascii_Params')),
					Handle
	;


/* Writes to the Log and ASCII download files */

	if (Svl(Log_Str)) {
		if (Global_Int('Com_Logging')) {
			Handle = Global_Int('Com_Log_Handle');
			Call WRITE_TO_FILE;
		}

		if (Global_Int('Com_ASCII_Download')) {
/* Filter CR and LF */
			if (Cr_Mode) {
				Term_Pos = 0;
FILTER_CR:
				Term_Pos = XPos('|13',Log_Str,Term_Pos + 1);
				if (Term_Pos) {
					if (Cr_Mode == 2) {
						Log_Str = Str_Del(Log_Str,Term_Pos,1);
						--Term_Pos;
					} else {
/* We are using the 2 character representation of a line feed that
Reconvert_String will decode later.  This will prevent the line feed filter
below from filtering the line feeds we have inserted here. */
						Log_Str = Str_Ins('\L',Log_Str,Term_Pos + 1);
					}
					Goto FILTER_CR;
				}
			}

			if (Lf_Mode) {
				Term_Pos = 0;
FILTER_LF:
				Term_Pos = XPos('|10',Log_Str,Term_Pos + 1);
				if (Term_Pos) {
					if (Lf_Mode == 2) {
						Log_Str = Str_Del(Log_Str,Term_Pos,1);
						--Term_Pos;
					} else {
						Log_Str = Str_Ins('|13',Log_Str,Term_Pos);
						++Term_Pos;
					}
					Goto FILTER_LF;
				}
			}
			if (Cr_Mode == 1) {
				Log_Str = Reconvert_String(Log_Str);
			}

			Handle = Global_Int('Com_ASCII_Handle');
			Call WRITE_TO_FILE;
		}

		Set_Global_Str('Com_Log_Str','');

	}
	Return_Int = 0;
	Goto EXIT;

WRITE_TO_FILE:
	Error = S_Write_Bytes(Log_Str,Handle,Bytes_Written);
	if (Svl(Log_Str) > Bytes_Written) {
		Return_Int = 3241;
		RM('COM_LOG_ERROR /A=1');
		Goto ABORT_LOG;
	}
	if (Error) {
		Return_Int = Error;
		RM('COM_LOG_ERROR /A=1');
		Goto ABORT_LOG;
	}

	RET;

ABORT_LOG:
	Return_Int = -1;

EXIT:

}

macro COM_INIT DUMP {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_INIT

Description:	The comunications module initialization routine.  Creates a
window to load the file 'MECOM.DB' and gets all the initialization parameters
and stores them into global variables.  If the file or any part of the file is
not found, then defaults will be assigned.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
/*
Set_Global_Str('Com_General_Params','/GENERAL=/DP=/UP=/LF=MECOM.LOG/A=1/VWW=0/VRM=0/MWW=0/MRM=0');
Set_Global_Str('Com_Box_Params','/BOX=/X1=10/Y1=5/X2=77/Y2=23');
Set_Global_Str('Com_Color_Params','/COLOR=/F=15/B=0');
Set_Global_Str('Com_Screen_Params','/SCREEN=/FS=1');
Set_Global_Str('Com_Modem_Params','/MODEM=/XP=~/XC=!/XT=^/XE=||/I=ATE1/H=~+++~ATH0!/DS=!/DC=ATDT/CS=CONNECT/HM=1/NC1=BUSY/NC2=VOICE/NC3=NO CARRIER/NC4=/RT=30/RP=2');
Set_Global_Str('Com_Terminal_Params','/TERMINAL=/T=DUMB/D=0/CI=0/CO=0/BT=1/BK=8/W=0/BL=2');
Set_Global_Str('Com_Line_Params','/LINE=/C=3/B=4/P=0/D=3/S=0/IB=2000/OB=200/XON=0/DTR=1/RTS=1/A1=1016/I1=4/T1=0/A2=760/I2=3/T2=0/A3=1000/I3=4/T3=0/A4=744/I4=3/T4=0/A5=16928/I5=3/T5=0/A6=16936/I6=3/T6=0/A7=21024/I7=3/T7=0/A8=21032/I8=3/T8=0');
Set_Global_Str('Com_Ascii_Params','/ASCII=/E=0/S=0/PC=0/CP=15/LP=10/CU=0/LU=2/CD=0/LD=0');
*/
	int Start_Line,Search_Amount,Jx,T_Reg_Exp_Stat = Reg_Exp_Stat;

	Reg_Exp_Stat = True;


/* Look for INITCOM.ME in MECOM.DB */
	RM('Setconfig /DB=MECOM/T=INITCOM.ME/C=1');
/* Save the beginning position */
	Start_Line = C_Line;
	Eol;
/* Find the next page title or EOF */
	if (Search_Fwd('|12',0) == 0) {
		Eof;
		if (C_Col > 1) {
			Down;
		}
	}

	Search_Amount = C_Line - Start_Line;

	Error_Level = 0;
	Update_Status_Line;

	Return_Str = '/GENERAL=/DP=/UP=/LF=MECOM.LOG/A=1/VWW=0/VRM=0/MWW=0/MRM=0';
	Call RETRIEVE_DATA;

	Return_Str = '/BOX=/X1=10/Y1=5/X2=77/Y2=23';
	Call RETRIEVE_DATA;

	Return_Str = '/COLOR=/F=15/B=0';
	Call RETRIEVE_DATA;

	Return_Str = '/SCREEN=/FS=1';
	Call RETRIEVE_DATA;

	Return_Str = '/MODEM=/XP=~/XC=!/XT=^/XE=||/I=ATE1/H=~+++~ATH0!/DS=!/DC=ATDT/CS=CONNECT/HM=1/NC1=BUSY/NC2=VOICE/NC3=NO CARRIER/NC4=/RT=30/RP=2';
	Call RETRIEVE_DATA;

	Return_Str = '/TERMINAL=/T=DUMB/D=0/CI=0/CO=0/BT=1/BK=8/W=0/BL=2';
	Call RETRIEVE_DATA;

	Return_Str = '/LINE=/C=3/B=4/P=0/D=3/S=0/IB=2000/OB=200/XON=0/DTR=1/RTS=1/A1=1016/I1=4/T1=0/A2=760/I2=3/T2=0/A3=1000/I3=4/T3=0/A4=744/I4=3/T4=0/A5=16928/I5=3/T5=0/A6=16936/I6=3/T6=0/A7=21024/I7=3/T7=0/A8=21032/I8=3/T8=0';
	Call RETRIEVE_DATA;

	Return_Str = '/ASCII=/E=0/S=0/PC=0/CP=15/LP=10/CU=0/LU=2/CD=0/LD=0';
	Call RETRIEVE_DATA;

/* Search for remaining MECOM specific data, and if missing, create default
values. */
	Tof;
	if (Search_Fwd('%|12PHONELST.DB$',0) == 0) {
		Eof;
		if (C_Col > 1) {
			Down;
		}
		Put_Line('|12PHONELST.DB');
		Down;
		Put_Line('/C=1/W=28/QK=1/T=Name /DBF=N');
		Down;
		Put_Line('/C=1/W=40/QK=1/T=Phone/DBF=#');
		Down;
		Put_Line('/C=1/L=4/TP=10/QK=1/T=Baud rate:');
		Down;
		Put_Line('/C=2/TP=12/T=110   /DBF=B/DBV=1/MIN=1');
		Down;
		Put_Line('/C=2/TP=12/T=300   /DBF=B/DBV=3');
		Down;
		Put_Line('/C=2/TP=12/T=600   /DBF=B/DBV=4');
		Down;
		Put_Line('/C=2/TP=12/T=1200  /DBF=B/DBV=5');
		Down;
		Put_Line('/C=2/TP=12/T=2400  /DBF=B/DBV=6');
		Down;
		Put_Line('/C=2/TP=12/T=4800  /DBF=B/DBV=7');
		Down;
		Put_Line('/C=2/TP=12/T=9600  /DBF=B/DBV=8');
		Down;
		Put_Line('/C=2/TP=12/T=19200 /DBF=B/DBV=9');
		Down;
		Put_Line('/C=2/TP=12/T=38400 /DBF=B/DBV=2');
		Down;
		Put_Line('/L=4/C=16/TP=10/QK=1/T=Data bits:');
		Down;
		Put_Line('/L=5/C=17/TP=12/T=5 /DBF=D/DBV=1/MIN=1');
		Down;
		Put_Line('/L=6/C=17/TP=12/T=6 /DBF=D/DBV=2');
		Down;
		Put_Line('/L=7/C=17/TP=12/T=7 /DBF=D/DBV=3');
		Down;
		Put_Line('/L=8/C=17/TP=12/T=8 /DBF=D/DBV=4');
		Down;
		Put_Line('/L=4/C=30/TP=10/QK=2/T=pArity:');
		Down;
		Put_Line('/L=5/C=31/TP=12/T=None /DBF=P/DBV=1/MIN=1');
		Down;
		Put_Line('/L=6/C=31/TP=12/T=Odd  /DBF=P/DBV=2');
		Down;
		Put_Line('/L=7/C=31/TP=12/T=Even /DBF=P/DBV=3');
		Down;
		Put_Line('/L=4/C=44/TP=10/QK=1/T=Stop bits:');
		Down;
		Put_Line('/L=5/C=45/TP=12/T=1 /DBF=S/DBV=0');
		Down;
		Put_Line('/L=6/C=45/TP=12/T=2 /DBF=S/DBV=1');
		Down;
		Put_Line('/L=9/C=26/TP=10/QK=2/T=dUplex:');
		Down;
		Put_Line('/L=9/C=34/TP=12/T=Full/DBF=E/DBV=0');
		Down;
		Put_Line('/L=9/C=43/TP=12/T=Half /DBF=E/DBV=1');
		Down;
		Put_Line('/C=17/L=11/W=24/ML=254/QK=1/T=Macro to run/DBF=M');
		Down;
		Put_Line('/C=17/L=12/W=24/ML=40/QK=1/T=First name  /DBF=FN');
		Down;
		Put_Line('/C=17/L=13/W=24/ML=40/QK=1/T=Last name   /DBF=LN');
		Down;
		Put_Line('/C=17/L=14/W=24/ML=40/QK=5/T=passWord    /DBF=PW');
		Down;
		Put_Line('****START****');
		Down;
		Put_Line('N=American Cybernetics BBS#=1-602-968-1082B=6P=1D=4S=1');
		Down;
		Put_Line('N=Omen Technology Inc. BBS#=1-503-621-3746B=5P=1D=4S=1');
	}

	Tof;
	if (Search_Fwd('%|12DOWNLOAD.DB$',0) == 0) {
		Eof;
		if (C_Col > 1) {
			Down;
		}
		Put_Line('|12DOWNLOAD.DB');
		Down;
		Put_Line('/C=1/W=20/QK=10/T=protocol Name       /DBF=PN');
		Down;
		Put_Line('/C=1/W=39/ML=254/QK=1/T=Command line        /DBF=CL');
		Down;
		Put_Line('/TP=1/C=1/W=3/QK=1/T=ASCII code of character to send before transfer(0 = none)/DBF=IC');
		Down;
		Put_Line('/C=1/TP=10/QK=1/T=Prompt for file:');
		Down;
		Put_Line('/C=2/TP=12/T=Off/DBF=PF/DBV=1');
		Down;
		Put_Line('/L=5/C=10/TP=12/T=Single file/DBF=PF/DBV=2');
		Down;
		Put_Line('/L=5/C=26/TP=12/T=Multiple files/DBF=PF/DBV=3');
		Down;
		Put_Line('****START****');
		Down;
		Put_Line('PN=ASCIICL=MECOM^COM_ASCII_DOWNPF=2');
		Down;
		Put_Line('PN=XMODEM(meft)CL=COMUTIL^COM_MEFT_XFERPF=2IC=13');
		Down;
		Put_Line('PN=XMODEM-CRC(meft)CL=COMUTIL^COM_MEFT_XFERPF=2IC=13');
		Down;
		Put_Line('PN=XMODEM-1K(meft)CL=COMUTIL^COM_MEFT_XFERPF=2IC=13');
		Down;
		Put_Line('PN=YMODEM(meft)CL=COMUTIL^COM_MEFT_XFERPF=1IC=13');
		Down;
		Put_Line('PN=XMODEM(dsz)CL=COMUTIL^COM_DSZ_XFER /CMD=rx <DLDIR><FILE>PF=2');
		Down;
		Put_Line('PN=XMODEM-CRC(dsz)CL=COMUTIL^COM_DSZ_XFER /CMD=rc <DLDIR><FILE>PF=2');
		Down;
		Put_Line('PN=XMODEM-OVERTHR(dsz)CL=COMUTIL^COM_DSZ_XFER /CMD=ro <DLDIR><FILE>PF=2');
		Down;
		Put_Line('PN=YMODEM(dsz)CL=COMUTIL^COM_DSZ_XFER /CMD=rb <DLDIR>PF=1');
		Down;
		Put_Line('PN=YMODEM-G(dsz)CL=COMUTIL^COM_DSZ_XFER /CMD=rb -g <DLDIR> <FILE>PF=1');
		Down;
		Put_Line('PN=QMODEM-G(dsz)CL=COMUTIL^COM_DSZ_XFER /CMD=rx -g <DLDIR><FILE>PF=2');
		Down;
		Put_Line('PN=ZMODEM(dsz)CL=COMUTIL^COM_DSZ_XFER /CMD=rz <DLDIR>PF=1');
	}

	Tof;
	if (Search_Fwd('%|12UPLOAD.DB$',0) == 0) {
		Eof;
		if (C_Col > 1) {
			Down;
		}
		Put_Line('|12UPLOAD.DB');
		Down;
		Put_Line('PN=ASCIICL=MECOM^COM_ASCII_UPPF=2');
		Down;
		Put_Line('PN=XMODEM(meft)CL=COMUTIL^COM_MEFT_XFERPF=2IC=13');
		Down;
		Put_Line('PN=XMODEM-CRC(meft)CL=COMUTIL^COM_MEFT_XFERPF=2IC=13');
		Down;
		Put_Line('PN=XMODEM-1K(meft)CL=COMUTIL^COM_MEFT_XFERPF=2IC=13');
		Down;
		Put_Line('PN=YMODEM(meft)CL=COMUTIL^COM_MEFT_XFERPF=3IC=13');
		Down;
		Put_Line('PN=XMODEM(dsz)CL=COMUTIL^COM_DSZ_XFER /CMD=sx <FILE>PF=2');
		Down;
		Put_Line('PN=XMODEM-1K(dsz)CL=COMUTIL^COM_DSZ_XFER /CMD=sx -k <FILE>PF=2');
		Down;
		Put_Line('PN=YMODEM(dsz)CL=COMUTIL^COM_DSZ_XFER /CMD=sbPF=3');
		Down;
		Put_Line('PN=YMODEM-1K(dsz)CL=COMUTIL^COM_DSZ_XFER /CMD=sb -kPF=3');
		Down;
		Put_Line('PN=ZMODEM(dsz)CL=COMUTIL^COM_DSZ_XFER /CMD=szPF=3');
		Down;
		Put_Line('PN=ASCIICL=COMUTIL^COM_ASCII_UPPF=2');
	}

	Tof;
	if (Search_Fwd('%|12TERMINALS.DB$',0) == 0) {
		Eof;
		if (C_Col > 1) {
			Down;
		}
		Put_Line('|12TERMINALS.DB');
		Down;
		Put_Line('/C=1/T=Name/W=8/DBF=N');
		Down;
		Put_Line('/C=1/T=Macro/W=30/ML=254/DBF=M');
		Down;
		Put_Line('****START****');
		Down;
		Put_Line('N=DUMBM=COM_DUMB');
		Down;
		Put_Line('N=ANSIM=COM_ANSI');
		Down;
		Put_Line('N=VT100M=COM_ANSI /T=1');
	}

	GOTO EXIT;

RETRIEVE_DATA:
	Jx = XPos('=',Return_Str,2);
	Goto_Line(Start_Line);
	if (Search_Fwd(Copy(Return_Str,1,Jx),Search_Amount)) {
		Set_Global_Str('Com_' + Copy(Return_Str,2,Jx - 2) + '_Params',Get_Line);
	} else {
		Set_Global_Str('Com_' + Copy(Return_Str,2,Jx - 2) + '_Params',Return_Str);
		Eol;
		++Search_Amount;
		Insert_Mode = True;
		Cr;
		Put_Line(Return_Str);
	}
	RET;

EXIT:
	Reg_Exp_Stat = T_Reg_Exp_Stat;
/* save info about this window for future reference */
	Set_Global_Str('COM_INIT_WINDOW','/WI=' + Str(Window_Id) + '/SL=' +
									Str(Start_Line) + '/SA=' + Str(Search_Amount));
/* If we had to put any stuff in this file, save it. */
	if (File_Changed) {
		Save_File;
	}

}

macro COM_DUMB {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_DUMB

Description:	The comunications module dumb terminal emulation.

Parameters:
							/DO= A string to send to be processed by the terminal emulator.
									 If present, the emulator will simply process and display
									 the string and will not attempt to read any characters
									 from the com port.  Useful in logon scripts.  This is NOT
									 the normal mode of operation.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
/*
This macro writes data to the terminal box from the comport in dumb terminal
emulation.
 */
	int Port_Num,Max_X,Max_Y,Scroll,Wrap,Terminator_Index,Error,Temp_Integer,
					I_S_Length,Logging,ASCII_Download,Jx,Duplex,CR_LF_Out,Display_Only;
	str Input_String[25],Terminators[3],Display_Only_Str,MStr;
	char Ch,Bs_Char;


	Port_Num = Global_int('Com_Port_Num');
	Display_Only = XPos('/DO=',MParm_Str,1);
	if (Display_Only) {
		Display_Only_Str = Copy(MParm_Str,Display_Only + 4,255);
	}
	Bs_Char = Char(Parse_Int('/BK=',Global_Str('Com_Terminal_Params')));
	CR_LF_Out = Parse_Int('/CO=',Global_Str('Com_Terminal_Params'));
	Duplex = Global_Int('COM_DUPLEX');
	Logging = Global_Int('Com_Logging');
	ASCII_Download = Global_Int('Com_ASCII_Download');
	GotoXY_Vp((Global_Int('Com_Cursor_Pos') & $FF),
						(Global_Int('Com_Cursor_Pos') >> $08));

GET_STRING:
	if (Display_Only) {
		if (Display_Only_Str == '') {
			Goto EXIT;
		}
		Error = 20;
		Temp_Integer = 1;
		Terminator_Index = 0;
		while (Temp_Integer < 4) {
			Jx = XPos(Copy('|8|10|13',Temp_Integer,1),Display_Only_Str,1);
			if (Jx > 0) {
				if (Jx < Error) {
					Error = Jx;
					Terminator_Index = Temp_Integer;
				}
			}
			++Temp_Integer;
		}
		Input_String = Copy(Display_Only_Str,1,Error);
		Display_Only_Str = Str_Del(Display_Only_Str,1,Error);
	} else {
		Error = read_com_till(port_num, 40,'|8|10|13',Input_String,Terminator_Index);
	}
	I_S_Length = SVL(Input_String);
	if (I_S_Length) {
/* If there is nothing special about this chunk of chars, just write it so as to
be as fast as possible. */
		if ((Logging) | (ASCII_Download)) {
			if ((Length(Global_Str('Com_Log_Str')) + I_S_Length) > 512) {
				RM('COM_WRITE_L_A');
				if (Return_Int == -1) {
					Goto ABORT_LOG;
				}
			}

			if (Terminator_Index == 1) {
/* Back space */
				Set_Global_Str('Com_Log_Str',Copy(Global_Str('Com_Log_Str'),1,Length(Global_Str('Com_Log_Str')) - 1) + Copy(Input_String,1,Svl(Input_String) - 1));
			} else {
				Set_Global_Str('Com_Log_Str',Global_Str('Com_Log_Str') + Input_String);
			}
ABORT_LOG:
		}
		Write_VP(Input_String);
	}

	if (Check_Key) {
		Jx = Inq_Key(Key1,Key2,TERM,MStr);
		if ((Jx == 1) & (MStr != '')) {
/* exit to run the assigned macro */
			Goto EXIT;
		} else if (jx == 2) {
/* Run the keystroke macro */
			Pass_key(key1,key2);
		} else if (Key1 == 0) {
			if (Key2 == 250) {
				Goto EXIT;
			}
		} else {
			Ch = Char(Key1);
			if (Key1 == 8) {
				Ch = Bs_Char;
			}

TRY_AGAIN:
			error = write_com(port_num,Ch,Jx);
			if (Error == 7) {
				Delay(10);
				Goto TRY_AGAIN;
			}
		/* if CR xlation is CR/LF then send the LF */

			if (Ch == '|13') {
				if (Cr_Lf_Out == 1) {
LF_AGAIN:
					error = write_com(port_num,'|10',Jx);
					if (Error == 7) {
						Delay(10);
						Goto LF_AGAIN;
					}
				}
			}

			if (Duplex) {
WRITE_TO_SCREEN:
				Write_Vp(Ch);
DO_LOG:
				if ((Logging == True) | (ASCII_Download == True)) {
					if (Duplex) {
						if (Ch == Bs_Char) {
/* Back space */
							Set_Global_Str('Com_Log_Str',Copy(Global_Str('Com_Log_Str'),1,Length(Global_Str('Com_Log_Str')) - 1));
						} else {
							Set_Global_Str('Com_Log_Str',Global_Str('Com_Log_Str') + Ch);
						}
						if (Length(Global_Str('Com_Log_Str')) > 512) {
							RM('COM_WRITE_L_A');
							if (Return_Int == -1) {
								Goto ABORT_LOG2;
							}
						}
					}
				}
ABORT_LOG2:
			}
		}
	}
	Goto GET_STRING;

EXIT:
	Set_Global_Int('Com_Cursor_Pos',WhereX_VP | (WhereY_Vp << $08));
}

macro COM_ANSI {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_ANSI

Description:	The comunications module ANSI and VT100 terminal emulation.

Parameters:
							/DO= A string to send to be processed by the terminal emulator.
									 If present, the emulator will simply process and display
									 the string and will not attempt to read any characters
									 from the com port.  Useful in logon scripts.  This is NOT
									 the normal mode of operation.

							 (C) Copyright 1991 by American Cybernetics, Inc.

Special thanks to W.P. Fredell for speed up mods and bug fixes.

*******************************************************************************/

#define vt100 1

	str Ch = global_str('Com_Terminal_Params'),
					Input_String = global_str('Com_Cur_Scrn_Params'),
					Display_Only_Str,MStr[8],Temp_String,
					Sequence_Param[4],
					Keycodes,
					Keysend,
/* If you add anything to Terminators, be sure |27 is last! */
					Terminators[30] = 'HfmhlABCDnJsuKLM|27';

	int Terminators_Length = Svl(Terminators) - 1,
			Port_Num,Sequence_Index,Param_Int,Tries,Terminator_Index,Error,
					I_S_Length,Logging,Partial,Temp_Integer,Jx,ASCII_Download,
					Key_Pressed = 0, Timeout,Cr_Lf_Out,Duplex,Display_Only,
					T_S_Length,X,Y,
					No_Color = Parse_Int('/NC=',ch),
					vp_width = Parse_Int('/X2=',Input_String) -
										 Parse_Int('/X1=',Input_String) - 1,
					vp_length = Parse_Int('/Y2=',Input_String) -
											Parse_Int('/Y1=',Input_String) - 1,
					type = parse_int("/TT=",mparm_str)
					;

	char BS_Char = Char(Parse_Int('/BK=',ch));

	Display_Only = XPos('/DO=',MParm_Str,1);
	if (Display_Only) {
		Display_Only_Str = Copy(MParm_Str,Display_Only + 4,255);
	}

	if (Type == VT100) {
		Keycodes = '|71|72|80|75|77|79|119|132|59|60|61|62';
		keysend = "[H[A[B[D[C[K[L[MOPOQOROS";
	} else {
		Keycodes = '|71|72|80|75|77|79|119|132|65|66|90|63|64|88|61|62|86|67|68|59|60|84|85|87|89|92|91|93';
		keysend = "[H[A[B[D[C[K[L[MOqOrOsOtOuOvOwOxOyOpOpOPOQOROSOmOlOnOMOM";
	}


	CR_LF_Out = Parse_Int('/CO=',ch);
	Duplex = Global_Int('COM_DUPLEX');
	Port_Num = Global_int('Com_Port_Num');
	Logging = Global_Int('Com_logging');
	ASCII_Download = Global_Int('Com_ASCII_Download');

	x = global_int("com_cursor_pos") & $FF;
	y = global_int("com_cursor_Pos") >> 8;
	call POSITION_CURSOR;

	Partial = ((Global_Str('Partial_Sequence') != '') | (Global_Int('Partial_Term') > 0));
	if (Partial) {
PARTIAL_LOOP:
		Temp_String = Global_Str('Partial_Sequence');
		Set_Global_Int('Partial_Term',0);
		Set_Global_Str('Partial_Sequence','');
		Goto ESCAPE_SEQUENCE;
	}

GET_STRING:
	if (Display_Only) {
		if (Display_Only_Str == '') {
			Goto EXIT;
		}
		Error = 254;
		Temp_Integer = 1;
		Terminator_Index = 0;
		while (Temp_Integer < 7) {
			Jx = XPos(Copy('|8|10|13|27|12|9',Temp_Integer,1),Display_Only_Str,1);
			if (Jx > 0) {
				if (Jx < Error) {
					Error = Jx;
					Terminator_Index = Temp_Integer;
				}
			}
			++Temp_Integer;
		}

		Input_String = Copy(Display_Only_Str,1,Error);
		Display_Only_Str = Str_Del(Display_Only_Str,1,Error);
	} else {
		Error = read_com_till(port_num, 20,'|8|10|13|27|12|9',Input_String,Terminator_Index);
	}
	I_S_Length = SVL(Input_String);
	if (I_S_Length) {
/* If there is nothing special about this chunk of chars, just write it so as to
be as fast as possible. */
		if (Terminator_Index > 3) {
			Input_String = Copy(Input_String,1,I_S_Length - 1);
		}
		if ((Logging) | (ASCII_Download)) {
			if ((Length(Global_Str('Com_Log_Str')) + I_S_Length) > 512) {
				RM('COM_WRITE_L_A');
				if (Return_Int == -1) {
					Goto ABORT_LOG;
				}
			}

			if (Terminator_Index == 1) {
/* Back space */
				Set_Global_Str('Com_Log_Str',Copy(Global_Str('Com_Log_Str'),1,Length(Global_Str('Com_Log_Str')) - 1) + Copy(Input_String,1,Svl(Input_String) - 1));
			} else {
				Set_Global_Str('Com_Log_Str',Global_Str('Com_Log_Str') + Input_String);
			}
	ABORT_LOG:
		}

		Write_VP(Input_String);
		if (Terminator_Index == 4) {
ESCAPE_SEQUENCE:
			Call DECODE_SEQUENCE;
			if (Partial) {
				Set_Global_Int('Partial_Term',4);
				Set_Global_Str('Partial_Sequence',Temp_String);
				if (Key_Pressed) {
					Goto KEY_WAS_PRESSED;
				}
				Goto EXIT;
			}

			Write_VP(Input_String);
		} else if (Terminator_Index == 5) {
/* Process a form feed character by clearing the screen */
			Clr_Vp;
		} else if (Terminator_Index == 6) {
/* Process a tab character by moving to the next tab stop */
			x = (((Wherex_Vp / 8) + ((Wherex_Vp % 8) != 0)) * 8) + 1;
			y = wherey_vp;
			call POSITION_CURSOR;
		}
	}

	if (Check_Key) {
KEY_WAS_PRESSED:
		Key_Pressed = 0;
		Jx = Inq_Key(Key1,Key2,TERM,MStr);
		if ((Jx == 1) & (MStr != '')) {
/* exit to run the assigned macro */
				Goto EXIT;
		} else if (jx == 2) {
/* Run the keystroke macro */
			Pass_key(key1,key2);
		} else if (Key1 == 0) {
			if (Key2 == 250) {
				Goto EXIT;
			} else if (Key2 == 83) {
				ch = '|127';
				goto SEND_KEYSTROKE;
			} else if (Key2 == 118) {
				ch = '|27[H|27[2J';
				goto SEND_KEYSTROKE;
			} else if (jx = (XPos(Char(Key2),Keycodes,1))) {
				ch = '|27' + copy(Keysend,((jx - 1) * 2) + 1,2);
				Goto SEND_KEYSTROKE;
			}
		} else {
			if (type == VT100) {
				if (jx = XPos(Char(Key2),'|82|79|80|81|75|76|77|71|72|73|74|55|83|78',1)) {
					if (XPos(Char(Key1),'|48|49|50|51|52|53|54|55|56|57|45|42|46|43',jx) == jx) {
						ch = '|27O' + copy("pqrstuvwxymlnM",jx,1);
						Goto SEND_KEYSTROKE;
					}
				}
			}
			Ch = Char(Key1);
			if (Key1 == 8) {
				Ch = Bs_Char;
			}

SEND_KEYSTROKE:
/*
THIS CODE IS USEFUL FOR DEBUGGING KEYSTROKE PROCESSING
BEEP;
JX = 0;
TEMP_STRING = "";
WHILE (JX < SVL(CH)) {
	++JX;
	TEMP_STRING = TEMP_STRING + HEX_STR(ASCII(STR_CHAR(CH,JX))) + " ";
}
WRITE(TEMP_STRING	+ "           ",1,1,0,ERROR_COLOR);
GOTO GET_STRING;
*/
			error = write_com(port_num,Ch,Jx);
			if (Error == 7) {
				Delay(10);
				Goto SEND_KEYSTROKE;
			}
		/* if CR xlation is CR/LF then send the LF */

			if (Ch == '|13') {
				if (Cr_Lf_Out == 1) {
LF_AGAIN:
					error = write_com(port_num,'|10',Jx);
					if (Error == 7) {
						Delay(10);
						Goto LF_AGAIN;
					}
				}
			}

			if (Duplex) {
WRITE_TO_SCREEN:
				Write_Vp(Ch);
DO_LOG:
				if ((Logging == True) | (ASCII_Download == True)) {
					if (Duplex) {
						if (Ch == Bs_Char) {
/* Back space */
							Set_Global_Str('Com_Log_Str',Copy(Global_Str('Com_Log_Str'),1,Length(Global_Str('Com_Log_Str')) - 1));
						} else {
							Set_Global_Str('Com_Log_Str',Global_Str('Com_Log_Str') + Ch);
						}
						if (Length(Global_Str('Com_Log_Str')) > 512) {
							RM('COM_WRITE_L_A');
							if (Return_Int == -1) {
								Goto ABORT_LOG2;
							}
						}
					}
				}
ABORT_LOG2:
			}
		}
	}

	if (partial) {
		goto PARTIAL_LOOP;
	}

	Goto GET_STRING;


/*********************************** SUBROUTINES *******************************/
DECODE_SEQUENCE:
/* This routine decodes ANSI escape sequences */
	if (Partial == False) {
		Temp_String = '';
	}
	Partial = False;
	Timeout = System_Timer;

READ_AGAIN:
	Key_Pressed = Check_Key;
	if (Display_Only) {
		if (Display_Only_Str == '') {
			Set_Global_Int('Partial_Term',Terminator_Index);
			Goto EXIT;
		}
		Error = 254;
		Temp_Integer = 0;
		Terminator_Index = 0;
		while (Temp_Integer < Terminators_Length) {
			++Temp_Integer;
			Jx = XPos(Copy(Terminators,Temp_Integer,1),Display_Only_Str,1);
			if (Jx > 0) {
				if (Jx < Error) {
					Error = Jx;
					Terminator_Index = Temp_Integer;
				}
			}
		}
		Input_String = Copy(Display_Only_Str,1,Error);
		Display_Only_Str = Str_Del(Display_Only_Str,1,Error);
	} else {
		Error = read_com_till(port_num, 254,Terminators, Input_String,Terminator_Index);
	}

	I_S_Length = SVL(Input_String);
	Temp_String = Temp_String + Input_String;
	T_S_Length = SVL(Temp_String);
	if (I_S_Length == 0) {
		if ((Key_Pressed == True) | (Display_Only == True)) {
			Partial = True;
			Ret;
		} else if ((System_Timer - Timeout) > 36) {
/* If we haven't recieved anything we recognize from the com port in 2 seconds,
give up */
			Goto DECODE_ERROR;
		}
		Goto READ_AGAIN;
	}
	if (Terminator_Index == 0) {
		if ((Key_Pressed == True) | (Display_Only == True)) {
			Partial = True;
			Ret;
		} else if (T_S_Length > 200) {
			Goto DECODE_ERROR;
		} else {
			Goto READ_AGAIN;
		}
	}

	if (Terminator_Index == Svl(Terminators)) {
/* If we got another escape character before a terminator we recognize, write
it to the screen raw, and get the next escape sequence */
		Write_Vp('|27' + Str_Del(Temp_String,T_S_Length,1));
		Goto DECODE_SEQUENCE;
	}

	if (Str_Char(Temp_String,1) != '[') {
		Goto DECODE_ERROR;
	}

	Input_String = '';
	if (Terminator_Index > 9) {
		switch (terminator_index) {
			case 10 :
/* Device status reports. */
				Sequence_Index = 1;
				Call GET_SEQUENCE_PARAM;
				if (Param_Int == 5) {
/* Device status report, send an "all is well" reply */
					error = write_com(port_num,'|27[0n',Jx);
				} else if (Param_Int == 6) {
/* Cursor position report */
					error = write_com(port_num,'|27[' + Str(Wherey_Vp) + ';' +
															Str(Wherex_Vp) + 'R',Jx);
				}
				break;
			case 11 :
/* Erase display */
				Sequence_Index = 1;
				Call GET_SEQUENCE_PARAM;
				if (Param_Int < 1) {
/* Erase from cursor to bottom of screen, don't move cursor. */
					Error = Parse_Int('/X2=',Global_Str('Com_Cur_Scrn_Params')) -
						Parse_Int('/X1=',Global_Str('Com_Cur_Scrn_Params')) - 1;
					Temp_Integer = Wherey;
					Draw_Char(32,WhereX,whereY,Text_Color_VP,Error - WhereX_Vp + 1);
					Jx = Parse_Int('/Y2=',Global_Str('Com_Cur_Scrn_Params')) - 1;
					Tries = Parse_Int('/X1=',Global_Str('Com_Cur_Scrn_Params')) + 1;

					while (Temp_Integer < Jx) {
						++Temp_Integer;
						Draw_Char(32,Tries,Temp_Integer,Text_Color_VP,Error);
					}
				} else if (Param_Int == 2) {
/* Erase entire display. */
					if (type == vt100) {
						Text_Color_Vp = Parse_Int('/F=',Global_Str('Com_Color_Params')) |
												(Parse_Int('/B=',Global_Str('Com_Color_Params')) << 4);
					}
					Clr_Vp;
				}
				break;
			case 12 :
/* Save Cursor */
				Set_Global_Int('Com_Temp_Cursor',(Wherex_Vp) | (WhereY_VP << $08));
				break;
			case 13 :
/* Restore cursor */
				x = global_int("com_temp_cursor") & $FF;
				y = global_int("com_temp_cursor") >> 8;
				call POSITION_CURSOR;
				break;
			case 14 :
/* Erase line */
					Draw_Char(32,WhereX,whereY,Text_Color_VP,
						(Parse_Int('/X2=',Global_Str('Com_Cur_Scrn_Params')) -
						Parse_Int('/X1=',Global_Str('Com_Cur_Scrn_Params'))) -
						WhereX_Vp);
				break;
			case 15 :
/* Insert_Line */
				Scroll_Box_Dn(Parse_Int('/X1=',Global_Str('Com_Cur_Scrn_Params')) + 1,
											WhereY,
											Parse_Int('/X2=',Global_Str('Com_Cur_Scrn_Params')) - 1,
											Parse_Int('/Y2=',Global_Str('Com_Cur_Scrn_Params')) - 1,
											Text_Color_Vp);
				break;
			case 16 :
/* Delete_Line */
				Scroll_Box_Up(Parse_Int('/X1=',Global_Str('Com_Cur_Scrn_Params')) + 1,
											WhereY,
											Parse_Int('/X2=',Global_Str('Com_Cur_Scrn_Params')) - 1,
											Parse_Int('/Y2=',Global_Str('Com_Cur_Scrn_Params')) - 1,
											Text_Color_Vp);
				break;
		}
		Goto DECODE_EXIT;
	}
	Sequence_Index = 1;
	if (Terminator_Index > 5) {
		Call GET_SEQUENCE_PARAM;
		if (Param_Int == -1) {
			Param_Int = 1;
		}
		x = wherex_vp;
		y = wherey_vp;
		switch (terminator_index) {
			case 6 :
/* Cursor up */
				y = wherey_vp - param_int;
				break;
			case 7 :
/* Cursor down */
				y = wherey_vp + param_int;
				break;
			case 8 :
/* Cursor right */
				x = wherex_vp + param_int;
				break;
			case 9 :
/* Cursor left */
				x = wherex_vp - param_int;
				break;
		}
		call POSITION_CURSOR;
		Goto DECODE_EXIT;
	}
	if (Terminator_Index < 3) {
/* Cursor_Position */
		Call GET_SEQUENCE_PARAM;
		y = Param_Int;
		Call GET_SEQUENCE_PARAM;
		x = param_int;
		call POSITION_CURSOR;
		Goto DECODE_EXIT;
	}
	if (Terminator_Index == 3) {
/* Set graphics rendition. */
GRAPHICS_PARAMS:
		Call GET_SEQUENCE_PARAM;
		if (Param_Int > -1) {
			switch (param_int) {
				case 0 :
	/* All attributes off. */
					if (type == vt100) {
						Text_Color_Vp = Parse_Int('/F=',Global_Str('Com_Color_Params')) |
												(Parse_Int('/B=',Global_Str('Com_Color_Params')) << 4);
					} else {
						text_color_vp = $07;
					}
					break;
				case 1 :
	/* Bold on */
					Text_Color_VP = Text_Color_VP | $8;
					break;
				case 2 :
	/* Faint on */
					Text_Color_VP = Text_Color_VP & $F7;
					break;
				case 3 :
	/* Italic on.   sorry, can't do this one */
					break;
				case 4 :
					if (Global_Int("COM_MONOCHROME")) {
	/* Underline for monochrome */
						Text_Color_Vp = Text_Color_Vp | $1;
/* ANSI.SYS makes color screens change the forground to blue, which we find
unacceptable, especially if the background happens to be blue!  It has been
suggested that we do reverse video instead, but that is just as likely to
cause problems.  Unless someone has a better suggestion, we will simply ignore
it. */
					}
					break;
				case 5 :
	/* Blink */
					Text_Color_VP = Text_Color_VP | $80;
					break;
				case 7 :
	/* Reverse video */
					if (Type == VT100) {
						Text_Color_Vp = (Parse_Int('/B=',Global_Str('Com_Color_Params')) |
												(Parse_Int('/F=',Global_Str('Com_Color_Params')) << 4))
												| (Text_Color_Vp & $8) | (Text_Color_Vp & $80);
					} else {
						Text_Color_VP = 112 | (Text_Color_Vp & $8) | (Text_Color_Vp & $80);
					}
					break;
				case 8 :
	/* Concealed, both foreground and background are the same */
					Text_Color_Vp =
						((Text_Color_VP & $70) >> 4) |
						(Text_Color_Vp & $70);
/*
USE THIS METHOD IF YOU HAVE YOUR BLINK DISABLED AND BRIGHT BACKGROUND ENABLED
					Text_Color_VP =
						(Text_Color_VP >> 4) |
						(Text_Color_Vp & $F0);
*/
					break;
			default :
				if (No_Color == False) {
/* If the no color flag is set, don't decode these */

/***==WPF==********************************************************************
06-Oct-1990 - New routine to do color changes without the interminable
							"IF ... THEN" statements which slow everything to a crawl.
							The routine converts the ANSI color code (blue-green-red)
							to the DOS/IBM code (red-green-blue).
******************************************************************************/
					if (Param_Int > 39) {
						if (Param_Int == 48) {
/* Subscript background */
						} else if (Param_Int == 49) {
/* Superscript background */
						} else {
/* Background Color */
							Param_Int = Param_Int - 40;
							Text_Color_VP = (Text_Color_VP & $8F) |
								(
								(((Param_Int & $01) << 2) |
								(Param_Int & $02) |
								((Param_Int & $04) >> 2)
								) << 4);
						}
					} else if (Param_Int > 29) {
/* Foreground Color */
							Param_Int = Param_Int - 30;
							Text_Color_VP = (Text_Color_VP & $F8) |
								(
								(((Param_Int & $01) << 2) |
								(Param_Int & $02) |
								((Param_Int & $04) >> 2)
								));
					}
				}
			}
			Goto GRAPHICS_PARAMS;
		}
		Goto DECODE_EXIT;
	}
	if (Terminator_Index == 4) {
/* Set mode.  Don't really know how to do this. */
	}
	if (Terminator_Index == 5) {
/* Reset mode.  Don't really know how to do this. */
	}
	Goto DECODE_EXIT;

DECODE_ERROR:
	Input_String = '|27' + Temp_String;
	RET;

DECODE_EXIT:
	Input_String = '';
	RET;

GET_SEQUENCE_PARAM:
	++Sequence_Index;
	Sequence_Param = '';
	while (XPos(Copy(Temp_String,Sequence_Index,1),'0123456789',1)) {
		Sequence_Param = Sequence_Param + Copy(Temp_String,Sequence_Index,1);
		++Sequence_Index;
	}
	if (Sequence_Param == '') {
		Goto NO_GOOD;
	}
	Return_Int = Val(Param_Int,Sequence_Param);
	if (Return_Int) {
NO_GOOD:
		Param_Int = -1;
	}
	RET;

POSITION_CURSOR:
/* check for any out of range coordinates */
	if (x > vp_width) {
		x = vp_width;
	}
	if (x < 1) {
		x = 1;
	}
	if (y > vp_length) {
		y = vp_length;
	}
	if (y < 1) {
		y = 1;
	}
	gotoxy_vp(x,y);
	RET;

/*******************************************************************************/

EXIT:
	Set_Global_Int('Com_Cursor_Pos',WhereX_VP | (WhereY_Vp << $08));
}

macro COM_WAIT_FOR_STR {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_WAIT_FOR_STR

Description:	A comunications module macro.  Waits until a specified string
							or one of a set of strings is received from the com port or the
							specified timeout occurs.  All characters recieved during this
							macro will be sent to the	terminal emulator and are subject to
							being interpreted and	displayed.

Parameters:
					/S1= - /Snn= The desired string(s).  If there is more than one,
					it will wait for the first occurance of any of them.
					/T=  If 0, timeout is infinite.  If > 0 number of seconds.
					/EI= Echo interval.  The time base is PC clock ticks, or 18.2 ticks
							 per second.  Characters will continue to accumulate in a 200 byte
							 buffer until the buffer is full, or the echo interval has lapsed.
							 The contents of the buffer are then sent to the terminal emulator
							 macro for displaying on the screen. The longer the interval, the
							 more characters can be potentially recieved before timeout
							 occurs.   The shorter the interval, the smoother the echo of
							 characters to the screen.  The reason for this trade-off is
							 because of the overhead of calling the terminal emulator to
							 display the accumulated characters.  The default is 9, or
							 approximately 1/2 second.

Returns:

					Return_Int > 0		Successful, nn = number corresponding to /Snn=.
					Return_Int = 0		timeout
					Return_Int = -1   User aborted by pressing <ESC>
					Return_Int = -2   Invalid parameters.  I.e, timeout is infinite,
														and there are no strings to wait for.  Obviously,
														this would cause an infinite loop.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str Display_Str,Match_Str,Mac_Str;
	int Error,Port_Num,End_Time,Timeout,Matching,Return_Code,Echo_Interval,Echo_End,
					String_Count,Match_Index;
	char Ch;

	String_Count = 0;
GET_STRINGS:
	Display_Str = Parse_Str('/S' + Str(String_Count + 1) + '=',MParm_Str);
	if (Display_Str != '') {
		++String_Count;
		Set_Global_Str('COM_MATCH_STR' + Str(String_Count),Caps(Display_Str));
		Goto GET_STRINGS;
	}

	Timeout = Parse_Int('/T=',MParm_Str);
	if (Timeout < 1) {
		Timeout = 0;
	}
/* We will not allow an infinite timeout when there is no string to wait for */
	if (Timeout == 0) {
		if (String_Count == 0) {
			Return_Code = -2;
			Goto ABORT;
		}
	}

	port_num = global_int('COM_PORT_NUM');
	Mac_Str = 'MECOM^' + Global_Str('Com_Terminal_MACRO') + ' /DO=';
	Return_Str = Caps(Return_Str);
	Display_Str = '';
	Match_Str = '';
	Matching = False;

	Echo_Interval = Parse_Int('/EI=',Mparm_Str);
	if (Echo_Interval < 1) {
		Echo_Interval = 9;
	}

	Echo_End = MemP($46C) + Echo_Interval;
	End_Time = Echo_End + Int_R(Real_I(Timeout) * 18.2);
	match_index = 1;

WAIT_LOOP:

	Error = read_com(port_num,Ch);

	if (Error == 0) {
		Display_Str = Display_Str + Ch;
		if (Matching) {
			Match_Str = Match_Str + Caps(CH);
FIRST_CHAR_MATCH:
			if (Match_Str != Copy(Global_Str('COM_MATCH_STR' + Str(Match_Index)),
					1,Svl(Match_Str))) {
/* Check to see if any of the other strings match before giving up */
				if (String_Count > 1) {
					Match_Index = 1;
LOOK_FOR_OTHER_MATCH:
					if (Match_Str == Copy(Global_Str('COM_MATCH_STR' + Str(Match_Index)),
							1,Svl(Match_Str))) {
						Goto NEW_MATCH;
					}
					if (Match_Index < String_Count) {
						++Match_Index;
						Goto LOOK_FOR_OTHER_MATCH;
					}
				}
				Matching = False;
				Goto NEW_MATCH;
			}
			if (Svl(Match_Str) == Length(Global_Str('COM_MATCH_STR' + Str(Match_Index)))) {
				Return_Code = Match_Index;
				Goto EXIT;
			}
		} else {
			Match_Index = 1;
CHECK_MATCH_BEGIN:
			if (Caps(Ch) == Copy(Global_Str('COM_MATCH_STR' + Str(Match_Index)),1,1))
				{
				Match_Str = Caps(Ch);
				Matching = True;
				Goto FIRST_CHAR_MATCH;
			} else {
				if (Match_Index < String_Count) {
					++Match_Index;
					Goto CHECK_MATCH_BEGIN;
				}
			}
		}
	}

NEW_MATCH:
	if (Timeout) {
		if (MemP($46C) >= End_Time) {
			Return_Code = 0;
			Goto EXIT;
		}
	}
	if ((Svl(Display_Str) > 200) | (MemP($46C) >= Echo_End)) {
		Echo_End = MemP($46C) + Echo_Interval;
		if (Svl(Display_Str)) {
			RM(Mac_Str + Display_Str);
		}
		Display_Str = '';
	}
	if (Key1 == 27) {
		Goto EXIT;
	}
	Check_Key;
	if (Key1 == 27) {
		Return_Code = -1;
		Goto ABORT;
	}
	Goto WAIT_LOOP;

EXIT:
	if (Display_Str != '') {
		RM(Mac_Str + Display_Str);
	}

ABORT:
	while (String_Count) {
		Set_Global_Str('COM_MATCH_STR' + Str(String_Count),'');
		--String_Count;
	}

	Return_Int = Return_Code;
}

macro COM_SEND_STR {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_SEND_STR

Description:	A comunications module macro.  Sends the specified string
							to the com port.

Parameters:
					Return_Str = Desired string.
					/T=  If 0, timeout is infinite.  If > 0 number of seconds.
					/CP=  Character pacing or delay between characters.  Only needed if
								remote is slow.

Returns:

					Return_Int = 1		Successful, string sent.
					Return_Int = 0		timeout
					Return_Int = -1		aborted by the user pressing <ESC>

					Return_Str = string sent (can be useful in case of timeout to
											 determine if part of the string was sent).

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Error,Port_Num,End_Time,Timeout,Pointer,jx,Char_Pacing;
	int rl, rv;

	Timeout = Parse_Int('/T=',MParm_Str);
	if (Timeout < 1) {
		Timeout = 0;
	}
	rl = length( return_str );
	port_num = global_int('COM_PORT_NUM');
	Char_Pacing = Parse_Int('/CP=',MParm_Str);

	Pointer = 0;
	End_Time = System_Timer + Int_R(Real_I(Timeout) * 18.2);

SEND_LOOP:
	++Pointer;
RETRY:
	if (Check_Key) {
		if (Key1 == 27) {
			rv = -1;
			Goto EXIT;
		}
	}

	Delay(Char_Pacing);
	Error = Write_com(port_num,Copy(Return_Str,Pointer,1),Jx);
	if (Pointer >= rl) {
		rv = 1;
		Goto EXIT;
	}
	if (Timeout) {
		if (System_Timer >= End_Time) {
			--Pointer;
			rv = 0;
			Goto EXIT;
		}
	}
	if ((Error == 0) & (Jx == 1)) {
		Goto SEND_LOOP;
	} else {
		Goto RETRY;
	}

EXIT:
	if (rv == 1) {
EMPTY_BUFFER:
		if (System_Timer < End_Time) {
			rm("COM_PORT_STATUS");
			if (Parse_int("/OB=",return_Str) == 0) {
				Goto BUFFER_EMPTY;
			}
			Goto EMPTY_BUFFER;
		}
	}

BUFFER_EMPTY:
	Return_Str = Copy(Return_Str,1,Pointer);
	Return_Int = rv;
}

macro COM_CONNECT_WAIT {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_CONNECT_WAIT

Description:	A comunications module macro.  Waits for a connection for a user
specified time.  Will exit upon timeout or connect, whichever occurs first.  If
timeout, then it will hang up.  Places a box on the screen to alert user of
elapsed time.

Parameters:
					/AC= If 1, indicates that we have already made a connection.
							 Currently, only used when autoredial has already made the
							 connection.

Returns:
					Return_Int = 0		timeout
					Return_Int = 1		connection established

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int Terminator_Index,Temp_Integer,Result_Code,port_num,Box_X,Box_Y;
	str Modem_Response,Temp_Char;

	Result_Code = False;
	Box_X = 26;
	Box_Y = 12;
	Clr_Vp;
	Set_Global_Int('Com_Cursor_Pos',$101);
	port_num = global_int('COM_PORT_NUM');
	if (Parse_Int('/AC=',MParm_Str)) {
		Return_Int = True;
		Goto ALREADY_CONNECTED;
	}

/* Use predefined redial timeout spec as a timeout here */
	Temp_Integer = Parse_Int('/RT=',Global_Str('Com_Modem_Params'));
	Put_Box(Box_X,Box_Y,Box_X + 28,Box_Y + 3,0,M_B_Color,'WAITING FOR CONNECT',True);
	Write('Press <ESC> to hang up',Box_X + 3,Box_Y + 2,0,M_B_Color);
WAIT_FOR_CONNECT:
	if (Check_Key) {
		if (Key1 == 27) {
			Goto EXIT;
		}
	}
	Write(Str(Temp_Integer) + ' of ' +
		Parse_Str('/RT=',Global_Str('Com_Modem_Params')) + ' seconds. ',Box_X + 3,
		Box_Y + 1,0,M_B_Color);
	if (read_com_till(port_num,1,'',Modem_Response,
		Terminator_Index) == 0) {
/* This one should be the connect or no connect string */

MORE_RESPONSE:
		Delay(50);
		if (read_com_till(port_num,80,'|10|13',
			Temp_Char,Terminator_Index) == 0) {
			Modem_Response = Modem_Response + Temp_Char;
			if ((Terminator_Index == 0) & (Svl(Modem_Response) < 80)) {
				Goto MORE_RESPONSE;
			}
		}
		if (XPos(Parse_Str('/CS=',Global_Str('Com_Modem_Params')),Modem_Response,1)) {
			Result_Code = True;
			Goto EXIT;
		}
		Goto NOT_CONNECTED;
	} else {
NOT_CONNECTED:
		if (Temp_Integer == 0) {
			Goto EXIT;
		}
		Delay(1000);
		--Temp_Integer;
		Goto WAIT_FOR_CONNECT;
	}

EXIT:
	Kill_Box;
	if (Result_Code == 0) {
/* If unsuccessful, hang up */
		RM('MECOM^COM_PHONE /TP=3');
	}
	Return_Int = Result_Code;
ALREADY_CONNECTED:
}

macro COM_CURSOR_OFF {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_CURSOR_OFF

Description:	A comunications module macro.  Turns the cursor off.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
/* Use the BIOS interrupt $10 */
	R_AX = $0100;
	R_CX = $2000;
	Intr($10);
}

macro COM_CURSOR_ON {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	CURSOR_ON

Description:	A comunications module macro.  Turns the cursor on.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
/* Any reference to the system variable INSERT_MODE will reset the cursor */
	INSERT_MODE = INSERT_MODE;
}

macro COM_SCREEN_DUMP {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_SCREEN_DUMP

Description:	A comunications module macro.  Captures the terminal screen to
							a file or device.

Parameters:
							/MY= The Y coordinate to reference the prompt box to.
							/LINE= If this <> 0 this captures only 1 line, stores both
										 the characters and the attributes in the global string:
										 "COM_SCREEN_CAPTURE" + PARSE_STR("/LINE=",MPARM_STR)
										 This was created to support the capturing of individual
										 lines of the screen so they can be temporarily written
										 over and then restored to their original state.  No file
										 is written to.  The line captured will be referenced to
										 the very top of the screen, not the top of the terminal
										 screen.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Line_Counter,Column_Counter,Active_Page,Old_x,old_y,Menu_Y,Handle,
					Error,
					Capture_Line = Parse_Int("/LINE=",MParm_Str);
	str T_Str[400],Sstr[10];

	if (capture_line) {
		goto APPEND_FILE;
	}

	Menu_Y = Parse_Int('MY=',MParm_Str);
	if (Menu_Y < 1) {
		Menu_Y = 3;
	}

	sstr = ' /S=1';
	Return_Str = 'MECOM.SCR';

PROMPT:
		 RM('MEUTIL1^FILE_PROMPT /H=MECOM^DUMP/T=SCREEN CAPTURE FILE NAME');
	if (RETURN_INT) {
		if (Return_Str == '') {
			Beep;
			Goto PROMPT;
		}

		if (File_Exists(Return_Str)) {
/* Check to see if the file is a device like a printer.  If so, don't bother
with the menu that follows. */
			Error = S_Open_File(Return_Str,1,Handle);
			if (Error) {
				Error_Level = 3000 + Error;
				RM('MEERROR');
				Goto EXIT;
			}
			R_AX = $4400;
			R_BX = Handle;
			Intr($21);
			if (R_Flags & $01) {
				Error_Level = 3000 + R_AX;
				RM('MEERROR');
				Goto ABORT;
			}
			Line_Counter = R_DX;
			Error = S_Close_File(handle);
			if (Error) {
				Error_Level = 3000 + Error;
				RM('MEERROR');
				Goto EXIT;
			}
			if (Line_Counter & $80) {
				Goto NEW_FILE;
			}
			RM('USERIN^XMENU /B=1/T=1/X=16/Y=' + Str(Menu_Y + 7) +
								'/S=1/L=SCREEN CAPTURE FILE EXISTS' +
								'/M=Append(MECOM^*)Overwrite()aBort()');
			if ((Return_Int < 1) | (Return_Int == 3)) {
				Goto EXIT;
			}
			if (Return_Int == 1) {
				Error = S_Open_File(Return_Str,1,Handle);
				if (Error) {
					Error_Level = 3000 + Error;
					RM('MEERROR');
					Goto EXIT;
				}
				Error = S_Move_File_Ptr(Handle,2,0);
				if (Error) {
					Error_Level = 3000 + Error;
					RM('MEERROR');
					Goto ABORT;
				}
				Goto APPEND_FILE;
			}
			if (Return_Int == 2) {
				Goto NEW_FILE;
			}
		} else {
NEW_FILE:
			Error = S_Create_File(Return_Str,Handle);
			if (Error) {
				Error_Level = 3000 + Error;
				RM('MEERROR');
				Goto EXIT;
			}
		}
	} else {
		Goto EXIT;
	}
APPEND_FILE:

	RM("COM_CURSOR_OFF");
	old_x = WhereX_vp;
	old_y = whereY_vp;
	Line_Counter = 0;
	Column_Counter = 0;
	T_Str = '';
	R_AX = $0F00;
	Intr($10);
	Active_Page = R_BX;

	if (capture_Line) {
		Line_Counter = capture_line;
		Goto CAPTURE_LINE;
	}

	while (Line_Counter < Screen_Length) {
		++Line_Counter;
		Column_Counter = 0;
CAPTURE_LINE:
		T_Str = '';

/* Get the screen a line at a time */
		while (Column_Counter < Screen_Width) {
			++Column_Counter;
/* Get the character at the cursor */
			if (capture_line)	{
				GotoXY(Column_Counter,Line_Counter);
			} else {
				GotoXY_VP(Column_Counter,Line_Counter);
			}
			R_AX = $0800;
			R_BX = Active_Page;
			Intr($10);
			if (capture_line) {
/* Get attribute also if in capture_line mode */
				T_Str = T_Str + Char((R_AX >> 8) & $FF);
			}
			T_Str = T_Str + Char(R_AX & $FF);
		}
		if (capture_line) {
			Set_Global_Str("COM_SCREEN_CAPTURE" + str(capture_line),T_Str);
			goto LINE_EXIT;
		}

/* Send line to file or device */
		Return_Str = t_str + '|13|10';
		RM('MEUTIL3^PRINTSTR' + sstr + '/H=' + Str(Handle));
		sstr = ' ';
		if (Error_Level) {
			RM('MEERROR');
			Goto ABORT;
		}
	}

LINE_EXIT:
	Gotoxy_vp(old_x,old_y);

ABORT:

	Error = S_Close_File(handle);

EXIT:
	RM("COM_CURSOR_ON");

}

macro COM_SEND_MSG {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_SEND_MSG

Description:	A comunications module macro.  Creates a window to edit a message
							to send to the com port.  Very useful if you don't like the EMail
							editing system on the BBS you're using.  Both spell checking and
							search are enabled.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Active_Window,Wrap,Margin;

	Active_Window = Window_Id;
	Create_Window;

	Return_Str = "";
LOAD_PROMPT:
	RM('MEUTIL1^FILE_PROMPT /H=MECOM^ME/T=LOAD MESSAGE FILE OR <ESC> FOR NEW MESSAGE');
	if (Return_Int == 0) {
		File_Name = 'MECOM.MSG';
		File_Changed = False;
	} else {
		Error_Level = 0;
		Load_File(Return_Str);
		if ((Error_Level != 0) && (Error_Level != 3002)) {
			RM('MEERROR');
			Error_Level = 0;
			Return_Str = '';
			Goto LOAD_PROMPT;
		}
	}
	Error_Level = 0;

	Wrap = Parse_Int('/MWW=',Global_Str('COM_GENERAL_PARAMS'));
	Margin = Parse_Int('/MRM=',Global_Str('COM_GENERAL_PARAMS'));

/* The macro EDITWINDOW provides the editor like interface */
	RM('USERIN^EDITWINDOW /H=MECOM^ME/A=1/X=1/SE=1/SP=1/T=EDIT A MESSAGE TO SEND TO THE COM PORT/Y='
			+ Str(Min_Window_Row) + '/W=' + Str(Screen_Width) + '/L=' +
			Str(Max_Window_Row - Min_Window_Row - 2) + '/WW=' + Str(Wrap) + '/RM=' +
			Str(Margin));
//	if (File_Changed) {
		if (Return_Int) {
			Tof;
			while (Not(At_Eof)) {
/* Send each line to the com port adding EOL terminator, character pacing, and
 blank line expansion as configured by user */
				if ((Get_Line == '') &
						(Parse_Int('/S=',Global_Str('Com_Ascii_Params')) > 0)) {
					Return_Str = ' ';
				} else {
					Return_Str = '';
				}
				Return_Str = Return_Str + Get_Line + Copy('|13|10',1,Parse_Int('/CO=',
					Global_Str('COM_TERMINAL_PARAMS')) + 1);

				RM('MECOM^COM_SEND_STR /T=10/CP=' + Parse_Str('/CP=',Global_Str('Com_Ascii_Params')));
/* Look and see if there are any incomming characters to display */
				RM('MECOM^COM_WAIT_FOR_STR /T=1/EI=9');
				Down;
			}
		}
//	}

	Delete_Window;
	if (Switch_Win_Id(Active_Window)) { }
}

macro COM_VIEW_FILE {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_VIEW_FILE

Description:	A comunications module macro.  Creates a window to view and edit
							a file or the currently active log.  In the case of the log, it
							can serve as an enhanced "scroll back" feature.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int Active_Window,View_Window,Log_Handle,Bytes_Written,Error,Menu_Y,
					Viewing_Log,TBC,Wrap,Margin;

	Menu_Y = Parse_Int('MY=',MParm_Str);
	Viewing_Log = False;
	TBC = Box_Count;
	View_Window = 0;
	Wrap = Parse_Int('/VWW=',Global_Str('COM_GENERAL_PARAMS'));
	Margin = Parse_Int('/VRM=',Global_Str('COM_GENERAL_PARAMS'));

	if ((Global_Int('Com_Logging') == True) &
		(Parse_Int('/LOG=',MParm_Str) == True)) {
/* Need to close the log file so we can load it into the window. */
		RM('COM_WRITE_L_A');
		if (Return_Int == -1) {
			Goto ABORT;
		}
		Error = S_Close_File(Global_Int('Com_Log_Handle'));
		if (Error) {
			Return_Int = Error;
			RM('COM_LOG_ERROR');
			Goto ABORT;
		}
		Set_Global_Int('Com_Logging',False);
		Set_Global_Str('Com_Log_Str','');
		Viewing_Log = True;
/* Word wrap is probably not desirable when viewing the log */
		Wrap = 0;
		Margin = 0;
	}


	Active_Window = Window_Id;
	Create_Window;
	View_Window = Window_Id;


/* Load the file. */
	if (Viewing_Log) {
		Return_Str = Global_Str('COM_LOG_FILE');
	} else {
		Return_Str = '';
LOAD_PROMPT:
		RM('MEUTIL1^FILE_PROMPT /H=MECOM^FILVIE/T=FILE TO VIEW');
		if (Return_Int == 0) {
			Goto ABORT;
		}
	}

	Error_Level = 0;
	Load_File(Return_Str);
	if ((Error_Level != 0) && (Error_Level != 3002)) {
		RM('MEERROR');
		Error_Level = 0;
		Return_Str = '';
		Goto LOAD_PROMPT;
	}
	Error_Level = 0;
	File_Name = Fexpand(Return_Str);
	File_Changed = False;
	if (Viewing_Log) {
		Eof;
		Goto_Col(1);
	}
VIEW_FILE:
	RM('USERIN^EDITWINDOW /H=MECOM^FILVIE/X=1/CC=1/SE=1/Y=' + Str(Min_Window_Row) + '/W=' +
			Str(Screen_Width) + '/L=' +	Str(Max_Window_Row - Min_Window_Row - 2) +
			'/WW=' + Str(Wrap) + '/RM=' + Str(Margin) +
			'/T=VIEWING' + copy(' LOG ',1,(Viewing_Log * 4) + 1) + 'FILE: ' + File_Name);


/* If they altered the file, give them a chance to save. */
	Read_Only = False;

	Kill_Box;
	RM('USERIN^CHECKFILE /H=WINDELETE');
	if (return_int <= 0) {
		GOTO VIEW_FILE;
	}


	if (Viewing_Log) {
	if (Switch_Win_Id(View_Window)) {
		Delete_Window;
	}
/* Now, reopen the log for append and move the file pointer. */
		Error = S_Open_File(Global_Str('COM_LOG_FILE'),1,Log_Handle);
		if (Error) {
			Return_Int = Error;
			RM('MECOM^COM_LOG_ERROR');
			Goto ABORT;

		}
		Error = S_Move_File_Ptr(Log_Handle,2,0);
		if (Error) {
			Return_Int = Error;
			RM('MECOM^COM_LOG_ERROR');
			Goto ABORT;
		}

	/* Set the log handle global to new handle. */
		Set_Global_Int('Com_LOG_Handle',Log_Handle);
		Set_Global_Int('Com_LOGGING',True);

	}

ABORT:
	if (Switch_Win_Id(View_Window)) {
		Read_Only = False;
		Delete_Window;
	}
	Switch_Win_Id(Active_Window);
EXIT:
/* Kill any remaining boxes made during the running of this macro */
	while (Box_Count > TBC) {
		Kill_Box;
	}
}
