macro_file MEERROR;
/*******************************************************************************
													 MULTI-EDIT MACRO FILE MEERROR
MEERROR			- Main error handing
MESSAGEBOX	- Puts a message box on the screen
BEEPS				- Provides sound support for error messages, etc.
ERRMSG			- Returns verbose error messages from error_level

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

macro MEERROR FROM ALL TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: MEERROR

Description:	Multi-Edit's main error handling.

Parameters: /EM=str  Error message override.
							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str  estr[20] ;
	if(  error_level != 0  ) {
		Return_Str = parse_str('/EM=', mparm_str);
		if(  return_str == ''  ) {
			RM('ERRMSG');
		}
		if(  return_int == 1  ) {
			estr = 'WARNING';
		} else {
			estr = 'ERROR';
		}
		RM('MessageBox /B=2/T=' + estr + ' #' + str(error_level) + '/M=' + return_str );
		ERROR_LEVEL = 0;
	}
}

macro MESSAGEBOX FROM ALL TRANS2 {
/********************************MULTI-EDIT MACRO******************************

Name: MessageBox

Description: Puts a message box on the screen (using ERROR_COLOR).  Waits
	for the user to press a key.

Parameters:	/T=str		box title
						/M=str		message
						/B=n			beep count.
						/Y=n      The starting row position, the box will automatically
												be centered in the screen.
						/NW=n			Do NOT wait for a key, and don't kill the box.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  tr, w, tc, tx, jx, message_count,y, ox = wherex, oy = wherey ;
	str  tstr ;

	tr = refresh;
	refresh = false;
	update_virtual_display;
	reset_virtual_display;
	tStr = parse_str('/M=', mparm_str);
	y = parse_int('/Y=',mparm_str);
	if(  y == 0  ) {
		y = 4;
	}
	message_count = 1;
	tx = 1;
	w = 26;
	while(  (svl(tstr) > (screen_width - 6)) & (tx > 0)  ) {
		tx = screen_width - 6;
		while(  (str_char(tstr,tx) != ' ') & (tx > 0)  ) {
			--tx;
		}
		set_global_str( '!MBSTR!' + str(message_count), shorten_str(copy(tstr,1,tx)));
		jx = length(global_str('!MBSTR!' + str(message_count)));
		if(  jx > w  ) {
			w = jx;
		}
		++message_count;
		tstr = copy(tstr,tx + 1, 254);
	}

	tstr = shorten_str(tstr);
	jx = svl(tstr);
	if(  jx == 0  ) {
		--message_count;
	} else {
		set_global_str( '!MBSTR!' + str(message_count), tstr);
		if(  jx > w  ) {
			w = jx;
		}
	}

	tx = (screen_width / 2) - (w / 2 );
	tc = tx - 1;
	if(  w < 24  ) {
		w = 24;
		tc = (screen_width / 2) - (w / 2 ) - 1;
	}
	if(  (y + message_count + 2) > (screen_length)  ) {
		y = screen_length - message_count - 3;
	}
	Put_Box(tc,y,tc + w + 4,y + 3 + message_count,0,Error_Color,parse_str('/T=', mparm_str),true);
	jx = 0;
	while(  jx < message_count  ) {
		++jx;
		WRITE(global_str('!MBSTR!' + str(jx)),tx, y + jx, 0, Error_Color);
	}
	gotoxy(0,0);
	RM('BEEPS /C=' + parse_str('/B=',mparm_str));
	if(  parse_int('/NW=',mparm_str) == 0  ) {
		while(  Check_Key  ) {
		}

		Write('<Press ANY key to continue>',(screen_width / 2) - 13 , y + 1 + message_count,0,error_Color);
		READ_KEY;
		Kill_Box;
	}
EXIT:
	jx = 0;
	while(  jx < message_count  ) {
		++jx;
		Set_Global_Str('!MBSTR!' + str(jx),'');
	}
	gotoxy( ox, oy );
	refresh = tr;

}

macro Beeps FROM ALL TRANS {
/********************************MULTI-EDIT MACRO******************************

Name: BEEPS

Description:  Beeps count number of times, at a given frequency and
							duration.  A step value may be passed to create staircase
							effects.

Parameters:
							/C=n		Count (# of beeps)
							/D=n		Duration in 100ths of a second (default = 60);
							/F=n		Frequency (default = 520)
							/P=n		Pause (time between beeps, default = 40)
							/S=n		Step frequency (for staircase effect, positive goes
														up, negative goes down, default = 0)

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	if (global_int("@NO_BEEPS")) // special global integer to disable beeps
		goto EXIT;

	int
		frequency = parse_int('/F=', mparm_str ),
		duration = parse_int('/D=', mparm_str ),
		pause = parse_int('/P=', mparm_str ),
		step = parse_int('/S=', mparm_str ),
		count = parse_int('/C=', mparm_str)
	;

	if (frequency == 0) {
		frequency = 520;
	}
	if (duration == 0) {
		duration = 60;
	}
	if (pause == 0) {
		pause = 40;
	}

	while(  count > 0  ) {
		--count;
		sound(frequency);
		delay(duration);
		sound(0);
		delay(pause);
		frequency = frequency + step;
	}
EXIT:
}


macro ERRMSG FROM ALL TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: ERRMSG

Description:	Returns a message string (in RETURN_STR) corresponding to the
						value in ERROR_LEVEL.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	Return_Str = '';

	return_int = 0;

	if(  ERROR_LEVEL == 0  ) {
		GOTO DO_ERROR;
	}

	/*  Multi-Edit errors  */
	switch ( Error_Level )
	{
		case 1001 :
			Return_Str = 'No Windows Left';
			break;

		case 1002 :
			Return_Str = 'Insufficient Memory to complete operation';
			break;

		case 1003 :
			Return_Str = 'Maximum line length reached';
			break;

		case 1004 :
			Return_Str = 'Operation Not Yet Supported';
			break;

		case 1006 :
			Return_Str = 'Invalid Numeric Input';
			break;

		case 1007 :
			Return_Str = 'HELP FILE not found';
			break;

		case 1010 :
			Return_Str = 'Invalid Search String';
			break;

		case 1011 :
			Return_Str = 'Input file NOT FOUND.  Save operation aborted.';
			break;

		case 1012 :
			Return_Str = 'File is marked read-only.';
			break;

		case 1013 :
			Return_Str = 'Save aborted by user.';
			break;

		case 1015 :
			Return_Str = 'Operation aborted by user.';
			break;

		case 1018 :
			Return_Str = 'Memory temporarily got low:  UNDO buffers may have been purged.  This is strictly an informative message, memory has been recovered and there is no cause for alarm.';
			return_int = 1;
			break;

		case 1016 :
			Return_Str = 'No Memory left for UNDO buffer.';
			return_int = 1;
			break;

		case 1017 :
			Return_Str = 'Error saving backup file.';
			break;

		case 1020 :
			Return_Str = 'Unable to create temporary file... Please make sure your Temporary File Directory is valid.';
			break;

		case 1030 :
			Return_Str = 'Invalid DIR Window Number.';
			break;

		case 1031 :
			Return_Str = 'DIR Window Not Open.';
			break;

		case 1032 :
			Return_Str = 'Number of directory entries exceeded 600.';
			break;

		case 1050 :
			Return_Str = 'Operation too large to store in undo buffer.';
			break;

/*  DOS errors  */
		case 2002 :
			Return_Str = 'Printer not ready.';
			break;

		case    2 :
		case 3002 :
			Return_Str = 'File Not Found';
			break;

		case 3003 :
			Return_Str = 'Path Not Found';
			break;

		case 3004 :
			Return_Str = 'Out of file handles';
			break;

		case 		5 :
		case 3005 :
			Return_Str = 'Access Denied';
			break;

		case 3007 :
			Return_Str = 'Memory Control blocks Destroyed';
			break;

		case 3008 :
			Return_Str = 'Insufficient Memory';
			break;

		case 3010 :
			Return_Str = 'Invalid Environment';
			break;

		case 3015 :
			Return_Str = 'Invalid Drive';
			break;

		case 3018 :
			Return_Str = 'No More Files';
			break;

		case 3021 :
		case 3152 :
			Return_Str = 'Device not ready';
			break;

		case 3031 :
		case 3162 :
			Return_Str = 'General Failure';
			break;

		case 3150 :
			Return_Str = 'Write protect Error';
			break;

		case 3153 :
			Return_Str = 'Unknown Command';
			break;

		case 3154 :
			Return_Str = 'Data Error';
			break;

		case 3158 :
			Return_Str = 'Sector Not Found';
			break;

		case 3159 :
			Return_Str = 'Printer out of Paper';
			break;

		case 3160 :
			Return_Str = 'Write fault';
			break;

		case 3161 :
			Return_Str = 'Read fault';
			break;

		case 3241 :
			Return_Str = 'Disk Full';
			break;

		case 4000 :
			return_str = 'Insufficient memory for Windows clipboard operation.  Try reducing size of text block.';
			break;
		case 4001 :
			return_str = 'Not running under Windows Enhanced Mode.';
			break;
		case 4002 :
			return_str = 'Windows returned an error on clipboard operation.';
			break;

		case 5001 :
			Return_Str = 'Macro file "' + Truncate_Path(error_str) + '" NOT Found in '
			+ ME_Path + ' nor ' + ME_Path + 'MAC\';
			break;

		case 5005 :
			Return_Str = 'Macro being loaded is an incompatible version';
			break;

		case 5006 :
			Return_Str = 'Macros may not be debugged when loaded in EMS';
			break;

		case 8001 :
			Return_Str = 'Link not found.';
			break;

		case 8002 :
			Return_Str = 'Help file not found.';
			break;

		case 8003 :
			Return_Str = 'Path to help files not found.';
			break;

		case 8004 :
			Return_Str = 'Out of file handles';
			break;

		case 8010 :
			Return_Str = 'No occurrences found.';
			return_int = 1;
			break;

		case 8011 :
			Return_Str = 'Incompatible index format.';
			break;

		case 8037 :
			Return_Str = 'No help files found in help path.';
			break;
	}

DO_ERROR:
}
