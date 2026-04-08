macro_file COMUTIL;
/*******************************************************************************
												MULTI-EDIT MACRO FILE COMUTIL

COM_MEFT_XFER			- File transfer interface to the internal file transfer MEFT.
COM_DSZ_XFER			- File transfer interface to the external program DSZ.
COM_LOG_MAJORBBS	- Auto log-on macro for Galacticom Majorbbs.
COM_LOG_WBBS			- Auto log-on macro for WBBS.
COM_LOG_COMPU			- Auto log-on macro for Compuserve.
COM_LOG_RBBS			- Auto log-on macro for most RBBS systems.
COM_LOG_PCBOARD		- Auto log-on macro for most PCBOARD systems.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

macro COM_MEFT_XFER TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_MEFT_XFER

Description:	A comunications module macro.  Provides an interface to the
internal Multi-Edit file transfer routine(MEFT).

Parameters:
							/MY= The Y coordinate to reference screen output to.
							/DIR= The direction of the transfer.  Legal values are:
									UP  -  for upload
									DOWN  -  for download
							/FN= The file name.  Not needed if YMODEM batch.
							/BATCH= Only needed if /PRO=YMODEM and batch.  It is the name of
											the file list for YMODEM batch uploading.
							/PRO= The protocol name.  Legal names are:
									XMODEM
									XMODEM-CRC
									XMODEM-1K
									YMODEM

Returns:
							Return_Int = 0 - Transfer was probably successful.
							Return_Int = -1 - Transfer was unsuccessful.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str  Direction[8], Tstr, Protocol[20]
						;
	int Menu_Y,Error,Port_Num,jx, prot, xdir;

	port_num = global_int('COM_PORT_NUM');
	menu_y = 5;
	Direction = Parse_Str('/DIR=',MParm_Str);
	Tstr = Parse_Str('/FN=',MParm_Str);
	Protocol = Caps(Parse_Str('/PRO=',MParm_Str));
	Error = XPos('(',Protocol,1);
	if(  (Error)  ) {
		Protocol = Copy(Protocol,1,Error - 1);
	}
	if(  get_path(tstr) == ''  ) {
		if(  direction == 'UP'  ) {
			Return_Str = Parse_Str('/UP=',Global_Str('Com_General_Params'));
		} else {
			Return_Str = Parse_Str('/DP=',Global_Str('Com_General_Params'));
		}
		if(  (copy(return_str, length(return_str),1) != '\') & (return_str != '')  ) {
			return_str = return_str + '\';
		}
		tstr = return_str + tstr;
	}
	if(  protocol == 'XMODEM'  ) {
		prot = 0;
	} else if(  copy(protocol,1,10) == 'XMODEM-CRC'  ) {
		prot = 1;
	} else if(  copy(protocol,1,9) == 'XMODEM-1K'  ) {
		prot = 2;
	} else if(  copy(protocol,1,6) == 'YMODEM'  ) {
		prot = 3;
		if(  direction == 'UP'  ) {
			if(  parse_str('/BATCH=', mparm_str) != ''  ) {
				tstr = '@' + parse_str('/BATCH=', mparm_str );
			}
		}
	} else {
invalid_parameter:
		RM('MEERROR^MessageBox /T=ERROR/B=2/M=Invalid protocol name for MEFT file transfer.');
		goto exit;
	}
	if(  direction == 'UP'  ) {
		xdir = 0;
	} else {
		xdir = 1;
	}
	error = com_file_xfer( port_num, prot, xdir, tstr );
	write_com( port_num, '|19', jx );
	Return_Int = -1;
	if(  (Error == 10)  ) {
		Error = 0;
	}
	if(  error != 0  ) {
		if(  ((Error > 66) & (Error < 70))  ) {
			TSTR = Protocol + ' ' + Lower(Direction) +
			'load cancelled by ' + Remove_Space(Copy('sender  receiveruser',
			((Error - 67) * 8) + 1,8)) +	'.';
		} else {
			Return_Int = Error;
			RM('MECOM^COM_ERROR');
			Tstr = Protocol + ' error - ' + Return_Str;
			Return_Int = -1;
		}
	} else {
		if(  (Error_Level != 0)  ) {
				RM('Meerror');
				TSTR = Protocol + ' ' + Lower(Direction) +'load terminated.';
		} else {
			TSTR = Protocol + ' ' +
				Copy('receivesend',((Direction == 'UP') * 7) + 1,7) + ' of ' + TStr +
				' complete.';
			Return_Int = 0;
		}
	}
	Write(Tstr,6,Menu_Y + 9,0,M_S_Color);
	RM('MECOM^COM_XFER_BEEP');
	Read_Key;
	Kill_Box;
	write_com( port_num, '|17', jx );
exit:
}

macro COM_DSZ_XFER TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_DSZ_XFER

Description:	A comunications module macro.  Provides an interface to the
external protocol program DSZ.

Parameters:
							/CMD= The DSZ command(usually indicates protocol and direction),
							but may include the following aliases:
									<DLDIR>  Will insert the user-defined download directory
									specification if one exists.
									<ULDIR>  Will insert the user-defined upload directory
									specification if one exists.
									<FILE>  Will insert /FN=.

							/FN= The file name.  Not used if /BATCH= <> ''.
							/BATCH= If = '', then use /FN= for single file transfer.
							Otherwise, indicates the name of the file containing filenames
							for batch transfer.
							/NPX= if 1, don't use the DSZ portx command, but instead use the
										default COM1 or COM2 address and interrupt.  Only to be
										used if your version of DSZ does not support the portx
										command.
							/GSZ=1  Use GSZ instead of DSZ

Returns:
							Return_Int = 0 - Transfer was probably successful(Provided you
							don't use COMMAND.COM to run DSZ)
							Return_Int = -1 - Transfer was unsuccessful.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str Port_Str[2],Command_Line,Batch[80],
			Line_Params = Global_Str('Com_Line_Params');
	str dsz_name[12] = 'DSZ';
	int File_Count,Active_Window,Error,
      temp_color = text_color_vp,
			T_Int, gsz = parse_int('/GSZ=', mparm_str),
			Port_Num = global_int('COM_PORT_NUM');

	if( gsz )
			dsz_name = 'GSZ';

	Port_Str = Str(Port_Num);
	Command_Line = Parse_Str('/CMD=',MParm_Str);
/* Check for special aliases in command line and process accordingly */
	T_Int = XPos('<DLDIR>',Caps(Command_Line),1);
	if(  (T_Int > 0)  ) {
		Return_Str = Parse_Str('/DP=',Global_Str('Com_General_Params'));
/* Strip trailing backslash */
		Return_Str = Copy(Return_Str,1,Length(Return_Str) - 1);
		Command_Line = Copy(Command_Line,1,T_Int - 1) + Return_Str +
			Copy(Command_Line,T_Int + 7,255);
	}
	T_Int = XPos('<ULDIR>',Caps(Command_Line),1);
	if(  (T_Int > 0)  ) {
		Command_Line = Copy(Command_Line,1,T_Int - 1) +
			Parse_Str('/UP=',Global_Str('Com_General_Params')) +
			Copy(Command_Line,T_Int + 7,255);
	}
	T_Int = XPos('<FILE>',Caps(Command_Line),1);
	if(  (T_Int > 0)  ) {
		Command_Line = Copy(Command_Line,1,T_Int - 1) +
			Parse_Str('/FN=',MParm_Str) +
			Copy(Command_Line,T_Int + 6,255);
	}

	Batch = Parse_Str('/BATCH=',MParm_Str);
	if(  (Batch != '')  ) {
		Command_Line = Command_Line + ' @' + Batch;
	}

/* Save all current com port parameters, then close the com port with the
 hardware lines asserted as they were when open*/

/*We're going to use the standard COM_OPEN_PORT macro to re-open the com
 port after we're done, but we will have to fool it into using the current
 port parameters instead of what the user may have configured as the default.
 The way we do this is to save the global string COM_LINE_PARAMS into another
 global and rebuild it according to current com port parameters*/
	RM('MECOM^COM_PORT_STATUS');
	Set_Global_Str('Com_Line_Params',
			"/XON=" + Parse_Str("/XONXOFF=",Return_Str) +
			"/RTS=" + Parse_Str("/CTSRTS=",Return_Str) +
			"/DTR=" + Parse_Str("/DSRDTR=",Return_Str) +
			Return_Str + Line_Params);

	Set_Com(PORT_NUM,15,3);
	Close_Com(PORT_NUM);

/* This SHELL_TO_DOS statement uses COMMAND.COM run DSZ.  We leave it there
just in case the one below does not work.  Just be sure that the DOS path
in the environment includes the directory where DSZ.COM or DSZ.EXE resides!
	Shell_To_Dos( dsz_name + ' portx ' +
		Lower(Hex_Str(Parse_Int('/A' + Port_Str + '=',Global_Str('Com_Line_Params'))))
		+ ',' + Parse_Str('/I' + Port_Str + '=',
		Global_Str('Com_Line_Params')) + ' ' + Command_Line,True );
 */

/* This SHELL_TO_DOS statement uses the EXEC function to run DSZ.COM.  We use our
own path search routine using the DOS path.  The advantage here is that
we will return an error level, whereas COMMAND.COM will not */

	Return_Str = Get_Environment('PATH') + ';' + Me_Path;
	RM('PATHSEARCH /F=' + dsz_name + '.COM');
	if(  (return_int == 0)  ) {
/* If we can't find DSZ.COM, look for DSZ.EXE */
	 RM('PATHSEARCH /F=' + dsz_name + '.EXE');
	}
	if(  (return_Int == 0)  ) {
		ERROR_LEVEL = 3002;
		RM('MEERROR /EM=THE PROGRAM DSZ NOT FOUND!');
		Error = 3002;
		Goto NO_PROGRAM;
	 } else {
/* 01-31-90 03:38pm - I just discovered that not all versions of DSZ support the
portx command.  This is unfortunately very bad if you want to use it for
MECOM, since MECOM allows up to 8 com ports and you can specify any address
and interrupt you wish for each.  Portx is the only way I know to tell DSZ
an explicit address and interrupt.  If the user has a version of DSZ that does
not support portx, the only way he can use it in MECOM is if he restricts his
com port activity to COM1 or COM2, using the following addresses and
interrupts:
COM1 address 3F8 interrupt 4
COM2 address 2F8 interrupt 3

After the above criteria are met, the user can add to the macro command line
of every file transfer record that calls this macro:
/NPX=1
 */
		if(  (Parse_Int('/NPX=',Mparm_Str))  ) {
			return_str = Return_Str + ' port ' + Port_Str + ' ' + Command_Line;
		} else {
			return_str = Return_Str + ' portx ' +
			Lower(Hex_Str(Parse_Int('/A' + Port_Str + '=',Global_Str('Com_Line_Params'))))
			+ ',' + Parse_Str('/I' + Port_Str + '=',
			Global_Str('Com_Line_Params')) + ' ' + Command_Line;
		}
		if(gsz)
				t_int = 2;
		else
				t_int = 3;
		RM('MEUTIL1^EXEC /MEM=6250/CMD=0/SWAP=0/SCREEN=' + str(t_int) + '/T=DSZ FILE TRANSFER');
	 }

	Return_Int = 0;
	Error = Exit_Code;

/*Reopen com port in the same state as it was when closed*/
	RM('MECOM^COM_OPEN_PORT');
/*Restore the global string COM_LINE_PARAMS back the way it was*/
	Set_Global_Str('Com_Line_Params',Line_Params);

NO_PROGRAM:
	if(  (Error)  ) {
		Error_Level = Error;
		RM('MEERROR /EM=' + dsz_name + ' error: ' + str(Error));
		Return_Int = -1;
	} else if(  (Error_Level)  ) {
		RM('MEERROR');
		Return_Int = -1;
	} else {
		RM('MECOM^COM_XFER_BEEP');
	}
ABORT:
  text_color_vp = temp_color;
}

macro COM_LOG_MAJORBBS TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_LOG_MAJORBBS

Description:	A comunications module macro.  Automatically logs on to
Galacticom's MajorBBS boards if you are registered, and if you have your
user id in the first name field, and your password field entered in the
phone dialing list.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
/* Need to log on and confirm the prompts */

	str First_Name[9],Password[9];
	int Jx;

/* Take first name from phone number global */

	First_Name = Parse_Str('FN=',Global_Str('PHONE'));

	if(  (First_Name == '')  ) {
		RM('MEERROR^MessageBox /T=ERROR/M=Logon parameters not found!');
		Goto EXIT;
	} else {
		Password = Parse_Str('PW=',Global_Str('PHONE'));
	}

/* Wait for a connection */
	RM('MECOM^COM_CONNECT_WAIT ' + MParm_Str);
	if(  (Return_Int == 0)  ) {
		Goto EXIT;
	}

	RM('MECOM^COM_WAIT_FOR_STR /T=5/S1="new":');
	if(  (Return_Int < 1)  ) {
		return_str = '|13';
		RM('MECOM^COM_SEND_STR /T=10');
		RM('MECOM^COM_WAIT_FOR_STR /T=60/S1="new":');
	}

/* Wait for the user id prompt for 60 seconds */
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Send user id */
	Return_Str = First_Name + '|13';
	RM('MECOM^COM_SEND_STR /T=10');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Wait for the password prompt for 60 seconds */
	RM('MECOM^COM_WAIT_FOR_STR /T=60/S1=Password');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Send password */
	Return_Str = Password + '|13';
	RM('MECOM^COM_SEND_STR /T=10');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

EXIT:
}

macro COM_LOG_WBBS TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_LOG_WBBS

Description:	A comunications module macro.  Automatically logs on to WBBS
boards if you are registered, and if you have your first and last name, and
your password fields entered in the phone dialing list.  Optionally captures
all mail and/or messages to the file WBBS.LOG.

Parameters:
							/MSG=1 Capture messages to WBBS.LOG
							/MAIL=1 Capture Mail to WBBS.LOG
							/LOGOFF=1 Log off at the end.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str First_Name[20],Last_Name[20],Password[20],ASCII_NAME[80];
	int Jx,Resume_ASCII_DNLD,Capture,Capture_Mail,Capture_Messages,
					Log_Open;

/* Take first name from phone number global */

	First_Name = Parse_Str('FN=',Global_Str('PHONE'));

	if(  (First_Name == '')  ) {
		RM('MEERROR^MessageBox /T=ERROR/M=Logon parameters not found!');
		Goto EXIT;
	} else {
		Last_Name = Parse_Str('LN=',Global_Str('PHONE'));
		Password = Parse_Str('PW=',Global_Str('PHONE'));
	}

	Capture_Messages = Parse_Int('/MSG=',MParm_Str);
	Capture_Mail = Parse_Int('/MAIL=',MParm_Str);

/* Wait for a connection */
	RM('MECOM^COM_CONNECT_WAIT ' + MParm_Str);
	if(  (Return_Int == 0)  ) {
		Goto EXIT;
	}

/* Wait for the WBBS name prompt for 60 seconds */
	RM('MECOM^COM_WAIT_FOR_STR /T=60/S1=Enter your first and last name:');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Send first and last name */
	Return_Str = First_Name + ' ' + Last_Name + '|13';
	RM('MECOM^COM_SEND_STR /T=10');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Wait for the WBBS password prompt for 60 seconds */
	RM('MECOM^COM_WAIT_FOR_STR /T=60/S1=Password?');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Send password */
	Return_Str = Password + '|13';
	RM('MECOM^COM_SEND_STR /T=10');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

	if(  ((Capture_Messages & Capture_Mail) == 0)  ) {
		Goto EXIT;
	}

/* Wait for the WBBS mail prompt for 60 seconds */
/* Notice the two slashes(//) between the Y and n.  This is because, the
Parse_Str function used by COM_WAIT_FOR_STR to parse out the string will
return a single slash if a double slash is encountered */
	RM('MECOM^COM_WAIT_FOR_STR /T=60/S1=You have mail waiting.  Read it? (Y//n):'
			 + '/S2=Sorry, no mail for you today');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}
	Jx = Return_Int;

/* Begin an ASCII download to capture all mail, messages, etc. */
/* If we are already ASCII downloading, close that file and we will reopen it
at the conclusion of this macro. */

	if(  (Global_Int('COM_ASCII_DOWNLOAD'))  ) {
		Ascii_Name = Global_Str('COM_ASCII_FILENAME');
		RM('COM_ASCII_DOWN /QUIT=1');
		Resume_ASCII_DNLD = True;
	} else {
		Resume_ASCII_DNLD = False;
	}


/* If there is mail waiting, get it captured to the file */

	if(  (Jx == 1)  ) {
		Return_Str =  'Y|13';
		RM('MECOM^COM_SEND_STR /T=10');
		if(  (Return_Int < 1)  ) {
			Goto RESUME_ASCII;
		}

		if(  (Capture_Mail)  ) {
			Return_Str = 'WBBS.LOG';
			RM('COM_ASCII_DOWN /OVERWRITE=1');
			Log_Open = True;
		} else {
			Log_Open = False;
		}

MORE_MAIL:
/* Wait for 60 seconds for more mail */
		RM('MECOM^COM_WAIT_FOR_STR /T=60/S1=Mail:/S2=<Press ENTER>/S3=(? for menu):');
		if(  (Return_Int < 1)  ) {
			Goto CLOSE;
		}
		Jx = Return_Int;
		Return_Str =  '|13';
		if(  (Jx == 3)  ) {
			Return_Str = 'Z' + Return_Str;
		}
		RM('MECOM^COM_SEND_STR /T=10');
		if(  (Return_Int < 1)  ) {
			Goto CLOSE;
		}
		if(  (Jx < 3)  ) {
			Goto MORE_MAIL;
		}
	}

	if(  (Capture_Messages)  ) {
		if(  (Log_Open == False)  ) {
			Return_Str = 'WBBS.LOG';
			RM('COM_ASCII_DOWN /OVERWRITE=1');
			Log_Open = True;
		}
	} else {
		if(  (Log_Open)  ) {
			RM('COM_ASCII_DOWN /QUIT=1');
		}
		Log_Open = False;
	}

MORE_MESSAGES:
/* Wait for 60 seconds for more messages */
		RM('MECOM^COM_WAIT_FOR_STR /T=60/S1=(? for menu):/S2=<Press ENTER>/S3=Done with zip read');
		if(  (Return_Int < 1)  ) {
			Goto CLOSE;
		}
		if(  (Return_int < 3)  ) {
			Return_Str =  '|13';
			RM('MECOM^COM_SEND_STR /T=10');
			if(  (Return_Int < 1)  ) {
				Goto CLOSE;
			}
			Goto MORE_MESSAGES;
		}

/* Log off */
		if(  (Parse_Int('/LOGOFF=',MParm_Str))  ) {
			Return_Str =  'GY|13';
			RM('MECOM^COM_SEND_STR /T=10');
			if(  (Return_Int < 1)  ) {
				Goto CLOSE;
			}
		}


CLOSE:
/* Close the capture file */
		if(  (Log_Open)  ) {
			RM('COM_ASCII_DOWN /QUIT=1');
		}

RESUME_ASCII:
/* If we were downloading ascii before, resume by appending */

	if(  (Resume_ASCII_DNLD)  ) {
		Return_Str = Ascii_Name;
		RM('COM_ASCII_DOWN /APPEND=1');
	}

EXIT:
}

macro COM_LOG_COMPU TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	COM_LOG_COMPU

Description:	A comunications module macro.  Automatically logs on to
COMPUSERVE.  You must have your user id entered as the first name, and your
password entered in the phone dialing list.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str First_Name[20],Password[20];

/* Take first name from phone number global */

	First_Name = Parse_Str('FN=',Global_Str('PHONE'));

	if(  (First_Name == '')  ) {
		RM('MEERROR^MessageBox /T=ERROR/M=Logon parameters not found!');
		Goto EXIT;
	} else {
		Password = Parse_Str('PW=',Global_Str('PHONE'));
	}

/* Wait for a connection */
	RM('MECOM^COM_CONNECT_WAIT ' + MParm_Str);
	if(  (Return_Int == 0)  ) {
		Goto EXIT;
	}

/* Send a carriage return */
	Return_Str = '|13';
	RM('MECOM^COM_SEND_STR /T=10');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Wait for the host name prompt for 60 seconds */
	RM('MECOM^COM_WAIT_FOR_STR /EI=4/T=60/S1=Host Name');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Send Host name CIS */
	Return_Str = 'CIS|13';
	RM('MECOM^COM_SEND_STR /T=10');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Wait for the user id prompt for 60 seconds */
	RM('MECOM^COM_WAIT_FOR_STR /EI=4/T=60/S1=User ID');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Send user id (first name) */
	Return_Str = First_Name + '|13';
	RM('MECOM^COM_SEND_STR /T=10');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Wait for the password prompt for 60 seconds */
	RM('MECOM^COM_WAIT_FOR_STR /EI=4/T=60/S1=Password:');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Send password */
	Return_Str = Password + '|13';
	RM('MECOM^COM_SEND_STR /T=10');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

EXIT:
}

macro COM_LOG_RBBS TRANS {
/*******************************************************************************

Name:	COM_LOG_RBBS

Description:	A comunications module macro.  Automatically logs on to RBBS.
You must have your first name, last name, and password entered in the phone
dialing list.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str First_Name[20],Last_Name[20],Password[20];

/* Take first name from phone number global */

	First_Name = Parse_Str('FN=',Global_Str('PHONE'));

	if(  (First_Name == '')  ) {
		RM('MEERROR^MessageBox /T=ERROR/M=Logon parameters not found!');
		Goto EXIT;
	} else {
		Last_Name = Parse_Str('LN=',Global_Str('PHONE'));
		Password = Parse_Str('PW=',Global_Str('PHONE'));
	}

/* Wait for a connection */
	RM('MECOM^COM_CONNECT_WAIT ' + MParm_Str);
	if(  (Return_Int == 0)  ) {
		Goto EXIT;
	}

/* Send an escape character.  Seems most RBBS and PCBoards expect this */
	Return_Str = '|27';
	RM('MECOM^COM_SEND_STR /T=10');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Wait for the first name prompt for 60 seconds */
	RM('MECOM^COM_WAIT_FOR_STR /T=60/S1=What is your FIRST name?');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Send first name, last name, and password in RBBS shorthand */
	Return_Str = First_Name + ';' + Last_Name + ';' + Password + '|13';
	RM('MECOM^COM_SEND_STR /T=10');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

EXIT:
}

macro COM_LOG_PCBOARD {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:  COM_LOG_PCBOARD

Description:  Autologon for PCBoard

Parameters:  /ANSI=1	Answer YES to the ANSI GRAPHICS prompt.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str First_Name[20],Last_Name[20],Password[20];

/* Take first name from phone number global */

	First_Name = Parse_Str('FN=',Global_Str('PHONE'));

	if(  (First_Name == '')  ) {
		RM('MEERROR^MessageBox /T=ERROR/M=Logon parameters not found!');
		Goto EXIT;
	} else {
		Last_Name = Parse_Str('LN=',Global_Str('PHONE'));
		Password = Parse_Str('PW=',Global_Str('PHONE'));
	}

/* Wait for a connection */
	RM('MECOM^COM_CONNECT_WAIT ' + MParm_Str);
	if(  (Return_Int == 0)  ) {
		Goto EXIT;
	}

/* Wait for the first name prompt for 30 seconds */
	RM('MECOM^COM_WAIT_FOR_STR /T=30/S1=(enter)');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}
	if(  parse_int('/ANSI=', mparm_str)  ) {
		Return_Str = 'Y|13';
	} else {
		Return_Str = 'N|13';
	}
	RM('MECOM^COM_SEND_STR /T=10');

/* Wait for the first name prompt for 30 seconds */
	RM('MECOM^COM_WAIT_FOR_STR /T=30/S1=first name?');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Send first name, last name, and password in RBBS shorthand */
	Return_Str = First_Name + ' ' + Last_Name + '|13';
	RM('MECOM^COM_SEND_STR /T=10');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}
/* Wait for the password prompt for 30 seconds */
	Return_Str = "Password";
	RM('MECOM^COM_WAIT_FOR_STR /T=30/S1=?');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

/* Send first name, last name, and password in RBBS shorthand */
	Return_Str = Password + '|13';
	RM('MECOM^COM_SEND_STR /T=10');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}

	RM('MECOM^COM_WAIT_FOR_STR /T=30/S1=(Enter)');
	if(  (Return_Int < 1)  ) {
		Goto EXIT;
	}
	Return_Str = '|13';
	RM('MECOM^COM_SEND_STR /T=10');


EXIT:


}
