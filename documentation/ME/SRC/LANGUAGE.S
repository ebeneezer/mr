#INCLUDE WINDOW.SH

#define RC_NoMoreErrors       0
#define RC_NoErrors           1
#define RC_DisplayError       2
#define RC_ProblemParsing     3
#define RC_CantFind           4
#define RC_DENoBeep           5

prototype LANGUAGE {
  void AutoArrangErrSrc(str m_str);
}

macro_file LANGUAGE;
/*******************************************************************************
													 MULTI-EDIT MACRO FILE LANGUAGE

 10-11-92 12:35pm
		TMJ -  Added Global_Str('@TMP_FILE_PATH') to tell Multi-Edit where to put
						the MEERR.TMP file.

COMPILE           - Compile a program
CMPERROR          - Finds compiler errors then moves cursor to error
ERR_FIND_ERROR    - Brings up source file and moves cursor to error.
ERR_TURBO_PASCAL  - Special error processing for Turbo Pascal
ERR_BORLAND       - Special error processing for most Borland products
ERR_BATCOM        - Special error processing for Batcom batch file compiler
ERR_POWER_C20     - Special error processing for Power C V 2.0
ERR_POWER_C       - Special error processing for Power C V 1.0
ERR_POWER_C120    - Special error processing for Power C V 1.2 and 2.2
ERR_2500_AD_68    - Special error processing for 2500 AD 68000 assembler
ERR_DATABUS       - Special error processing for Databus compiler
ERR_SALFORD_FTN7  - Special error processing for Salford Fortran 77
ERR_GIMPEL_LINT   - Special error processing for Gimpel Lint
ERR_GIMPEL_LINT5  - Special error processing for Gimpel Lint v5.x
ERR_TELXON_TCAL   - Special error processing for Telxon TCal V 3.3
ERR_MICROSOFT     - Special error processing for most Microsoft compilers and
										many others with similar error output format.
ERR_TEX           - Special error processing for EMTEX
ERR_MFCOBOL       - Special error processing for Microfocus Cobol
ERR_MS_PASCAL     - Special error processing for Microsoft Pascal
ERR_C87_FB        - Special error processing for Clipper Summer 87 and Foxbase
ERR_REALIACOBOL   - Special error processing for Realia Cobol
ERR_CROSSCODE_C   - Special error processing for Cross Code C
ERR_ARCH_8051_C   - Special error processing for Archimedes 8051 C
ERR_AVMAC65       - Special error processing for Avocet AVMAC65
ERR_ACUCOBOL			- Special error processing for ACU COBOL
ERR_METAWARE			- Special error processing for Metaware High C and Metaware
										Professional Pascal
ERR_ASPECT				- Special error processing for PROCOMM's ASPECT script compiler
ERR_SVS_FORTRAN		- Special error processing for SVS FORTRAN compiler
ERR_MCC68000_C    - Special error processing for Mitrotec Research MCC68K C
ERR_2500AD_C      - Special error processing for 2500AD C Compiler
ERR_AMD_HIGHC29K  - Special error processing for Advanced Micro Devices HighC 29K
ERR_SASM          - Special error processing for SASM compiler
ERR_WATCOM_CPP    - Special error processing for WATCOM C/C++ compiler
ERR_MERIDIAN_ADA    - Special error processing for WATCOM C/C++ compiler
AUTOARRANGERRSRC  - Supports the Auto-Arrange compiler error window feature
TEMPLATE          - Calls the appropriate template macro for the language type
MATCH             - Calls the appropriate matching macro for the language type

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/

macro COMPILE {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: COMPILE

Description:  Compiles a program based on the setup for the filename extension
							of the current file.

							 (C) Copyright 1992 by American Cybernetics, Inc.

We would like to acknowledge the contributions of our user base which are too
numerous to mention here.
*******************************************************************************/

	str TStr,Tstr2,Red_Str[128],Comp_Str,trailer[30],Ext[3],E_F_Name[128],
					comp_name[60],ext_list[80];
	//  note comp_str above added for ALSYS
	int  meerr_id,
			 jx,T_Swap_Mem = 0,Clear_Screen_Stat,Compiler_Prompt,
			 Save_All,Reload_File,Dont_Process,Wait_4_key,Temp_Id,tel;

	/*  Since PLM does not put the file name on the primary file we need to save
		away the name of the primary fiie in case we find an error in an include file
		we will know when we press the next error key what file to go back to if
		the error is back in the primary file  */

// Get the compiler/program interface data for this extension
	Refresh = false;
	temp_id = window_id;
	meerr_id = 0;
	Ext = Get_Extension(File_Name);
	ext_list = parse_str("EXT=",global_str("." + ext));
	TStr = '';
	RM('GET_DB_RECORD /NDF=1/F=MECONFIG/DPT=' + Ext_list +
			'.PGM/GLO=!PGM/AGLO=!PGM/DBF=EXT/FV=...');
	if ( return_int != -2 ) {
		Tstr = Ext;
		jx = 1;
	} else {

// If we can't find a compiler for this extension, Check to see if there
//  might be a default compiler setup, and if so, see if this extension was
//  intended to fall under the default setup.

		RM('GET_DB_RECORD /NDF=1/F=MECONFIG/DPT=' +
			 'DEFAULT.PGM/GLO=!PGM/AGLO=!PGM/DBF=EXT/FV=...');
		if ( return_int != -2 ) {
			if ( (Ext == '') )
				Goto NO_EXTENSION;

			RM('GET_DB_RECORD /NDF=1/F=MECONFIG/DPT=EXT.DB/GLO=!PGM/DBF=EXT/FV=' + Ext_list);
			if ( Return_Int > -1 ) {
NO_EXTENSION:
				Jx = 1;
				TStr = 'DEFAULT';
			} else {
				TStr = '';
				Jx = 0;
			}
		}
	}

	Switch_Win_Id( temp_id );

	if ( Get_Extension(Global_Str('Last_Compiled_Window')) !=  Caps(Ext) )
		Set_Global_Int('DB#' + Ext + '.PGM',1);

	Set_Global_Str('Last_Compiled_Window',Caps(file_name));
	Clear_Screen_Stat = False;
	Compiler_Prompt = False;
	Save_All = False;

// Mod to handle correct placement of .TMP files
	RM("TMP_FILE_NAME /FN=MEERR.TMP");
/*
	return_str = Global_Str('@TMP_FILE_PATH');
  if(length(return_str) != 0)
  {
    RM('XLATECMDLINE');
    if(copy(return_str,length(return_str),1) != '\')
    {
      return_str += '\';
    }
    return_str += user_id + 'MEERR.TMP';
  }
  else
  {
    return_str := user_id + 'MEERR.TMP';
    rm('MakeUserPath');
  }
*/
  Red_Str =  return_str;
	e_f_name = Red_Str;

	if ( Jx == 0 ) {
		Compiler_Prompt = True;
		Tstr = '';
		Goto COMP_PROMPT;
	}

	if ( Global_Int('@PGM_MENU_BYPASS') ) {
// If user is configured to bypass the program menu, and there is only 1
// program interface defined, bypass it.
		if ( (Global_Int('!PGM') == 1) )
			Goto ONLY_ONE;
	}

	RM('USERIN^DB /2TOP=1/NDF=1/F=MECONFIG/HPT=DEFAULT.PGM/DPT=' + ext_list +
			'.PGM/GLO=!PGM/NOALPHA=1/LO=2/LT=(.' + TStr +
		')ÄSELECT A PROGRAM/DT=COMPILER-PROGRAM SETUP/H=FE_PROGRAM/ENC=1');

	Jx = Return_Int;

	if ( Jx == -1 ) {
		Compiler_Prompt = True;
		Tstr = '';
		Goto COMP_PROMPT;
	}

	if ( Jx == 0 ) {
		Make_Message('Compiler/program aborted.');
		Goto EXIT;
	}

ONLY_ONE:
// Deallocate global used to store amount of compiler interfaces
	Set_Global_Int('!PGM',0);
	TStr2 = Global_Str('!PGM');
	set_global_str('!PGM', '' );
// We must parse out the compiler command line before we capitalize TStr2 because
// some compliers command line options are case sensitive.
	TStr = Parse_Str('CL=',Tstr2);
	Comp_Name = Parse_Str('|127PN=', tstr2);
	TStr2 = Caps(Tstr2);
// Set swap memory in paragraphs
	T_Swap_Mem = (parse_int('MEM=',tstr2) * 1024) / 16;

	Clear_Screen_Stat = Parse_INT('CS=',TStr2);
// This will set COMPILER_PROMPT true if the parameter extists or if the command
// line is null
	Compiler_Prompt = (Parse_Int('CP=',TStr2)) || (Tstr == '');
	Save_All = Parse_int('SA=',TStr2);
	Reload_File = Parse_int('RF=',TStr2);
	Dont_Process = Parse_int('DP=',TStr2);
	Comp_Str = Parse_Str('PT=',TStr2);
	Wait_4_Key = Parse_Int('WAIT=',TStr2);
	Set_Global_Str('LAST_COMP',Comp_Str);


// now see if he has a command line
	if ( Tstr == '' ) {
//  no command line but we still have set the compiler setup flags
//  that the user put into file extension compiler/program fields
		Goto COMP_PROMPT;
	}

// If it exists then setup
	if ( TStr != '' ) {
		while ( 0 != (jx = XPOS('<NR>',Caps(TStr),1)) ) {
			Red_Str = '';
			TStr = Str_Del(Tstr,jx,4);
		}

// translate the <MEERR> metacommand
		while ( 0 != (jx = XPOS('<MEERR>',Caps(TStr),1)) ) {
			TStr = Str_Del(Tstr,jx,7);
			tstr = str_ins(e_f_name,tstr,jx);
		}

// parse the compiler command to insert filename, path and extensions
		Return_Str = TStr;
		RM('XlateCmdLine /F=' + File_Name);
		TStr = Return_Str;

		if ( Comp_Str == 'STONYBROOK MODULA-2' )
			del_file(truncate_path(truncate_extension(file_name)) + '.ERR');

// Special processing for PLM-86
//  Since we use a lot of compiler defaults I tack them on at the end of
//  the command line
//  Someday I am going to make a menu to choose the options from which will
//  build the command line for the user
//  setting the command line prompt to yes will allow the user to enter
//  his own command line for now

COMP_PROMPT:
	if ( ((Comp_Str == 'INTEL PLM-86') | (Comp_Str == 'INTEL IC-86')) & (TStr == '') )
	{
		if ( (Comp_Str == 'INTEL PLM-86') )
			TStr = 'PLM86 ' + file_name + ' ';
		else
			TStr = 'IC86 ' + file_name + ' ';

		return_str = tstr;
		rm('QUERYBOX /P=COMMAND LINE: /T=LA OT(3) NOLI DB PR(CON:) added to invocation line for you.'
				+ '/W=73/ML=120' );
		tstr = return_str;
		jx = return_int;
		if ( jx == 0 )
			TStr = '';

		kill_box;
		if ( TStr != '' ) {
			tstr = tstr + ' LA OT(3) NOLI DB PR(CON:)';
			Compiler_Prompt = False;
		} else {
			Make_Message('Compile line null.  Compile aborted.');
			Goto EXIT;
		}
	}

// Now I see if command line exists
// If it exists then setup
		if ( Compiler_Prompt ) {
			Return_Str = Tstr;
			RM('userin^QUERYBOX /H=CP/W=74/ML=128/T=ENTER COMPILER/PROGRAM COMMAND');
			if ( Return_Int == False ) {
				Make_Message('Compiler/program aborted.');
				Goto EXIT;
			}
			if ( Return_Str == '' ) {
				Make_Message('Compile line null.  Compile aborted.');
				Goto Exit;
			}
			TStr = Return_Str;
		}

// Get rid of meerr.tmp if it exists in memory.
		// Now find the error window
		jx = 0;
		while(  (jx < window_count) & (Caps(Truncate_Path(File_Name)) != (user_id + 'MEERR.TMP')) )
			Switch_Window(++jx);
		if ( caps(truncate_path(file_name)) == (user_id + 'MEERR.TMP') ) {
			erase_window;
			meerr_id = window_id;
		}
		Switch_Win_Id(temp_id);
		refresh = TRUE;
		new_screen;
		refresh = FALSE;

// Check the disk to see if MEERR.TMP is read only on disk and warn the user
		Jx = File_attr(E_F_Name);
		if ( Error_Level == 0  ) {
			if ( Jx & 1 ) {
				RM('MEERROR^Beeps /C=1');
				RM('userin^XMENU /H=CP/B=1/X=1/Y=2/L=FILE: ' + E_F_Name +
				' IS MARKED READ ONLY/M=Reset-read-only-attribute(CP)Abort()');
				if ( (Return_Int == 1) )
// Turn off read only flag if user wants to, otherwise abort compile
					Set_File_Attr(E_F_Name,Jx & $FFFE);
				else {
					Make_Message('Compiler/program aborted.');
					Goto EXIT;
				}
			}
		}
		else
			Error_Level = 0;

		// Save off source file only or all files if Save_All flag is set
		if ( Save_All ) {
			rm("AUTOSAVE");
			if( error_level )
				goto exit;
		} else if (file_changed) {
			RM("SAVEFILE /NP=1");
			if( error_level || !return_int ) {
				goto exit;
			}
		}

		Switch_Win_Id(temp_id);
		Refresh = True;
		Redraw;
		working;  // Turn on the working sign
		Refresh = False;   // Turn screen off
		if ( Reload_File ) {
// Need to do this if network file locking is turned on
			Return_Str = File_Name;
			Erase_Window;
			File_Name = Return_Str;
		}
		gotoxy(1, message_row );

/******* 10-11-89 07:20pm ********  */

			//  Special processing for 'MICROSOFT C /FS' and 'MICROSOFT MASM LIST'
			//  This depends on the switch '/FsMeerr.tmp' in the CL command line
			//  or 'Meerr.tmp' in the MASM command line to generate a listing file
			if ( (Comp_Str == 'MICROSOFT C /FS') ||
					 (Comp_Str == 'MICROSOFT MASM LIST') &&
					 (Red_Str != "") )
				Red_Str = 'NUL';
			//  END OF 'MICROSOFT C /FS' and 'MICROSOFT MASM LIST'


			//  Special processing for CLARION
			//  THE FOLOWING BAT FILE MUST BE USED FOR COMPILE(%2 is MEERR file)
			// ERASE %2
			// ERASE *.ERR
			// ccmp %1.cla yes /b
			// IF NOT EXIST *.ERR GOTO EXIT
			// FOR %%F IN (*.ERR) DO TYPE %%F >> %2
			// :EXIT
			// ALSO SET RED_STR TO BLANK FOR DATAFLEX
			if ( (Comp_Str == 'CLARION') || (comp_str == 'DATAFLEX') )
				Red_Str = '';
			//  END OF CLARION

			 if ( comp_str == 'DATAFLEX' )
					TRAILER = '';
			 else
					TRAILER = '|13EXIT|13';

		WORKING;
		MAKE_MESSAGE(Return_Str = TSTR);
			// Compile the program

		RM('MEUTIL1^EXEC /MEM='+STR(t_swap_mem)+'/SCREEN='+
				STR(Clear_Screen_Stat)+'/RED='+red_str+'/T='+
				comp_name+'/WAIT='+Str(Wait_4_Key));

		Refresh = False;   // Turn screen offf
		jx = error_level;  // Get the returned error
		Set_Global_Str('LAST_COMP',Comp_Str);

		if ( jx != 0 ) {
			Make_message('Unable to run compiler.');
			goto exit;
		}


// reload file if that flag is set
		if ( Reload_File ) {
			LOAD_FILE(File_Name);
			RM('EXTSETUP');
		}

		// For stonybrook, use the error file which is automatically created.
		if ( (Comp_Str == 'STONYBROOK MODULA-2') )
//			Shell_to_Dos('COPY ' + truncate_path(truncate_extension(file_name)) + '.ERR ' + user_id + 'MEERR.TMP > NUL',True);
			Shell_to_Dos('COPY ' + truncate_path(truncate_extension(file_name)) + '.ERR ' + e_f_name +  ' > NUL',True);

		// For TeX, use the log file which is automatically created.
		if ( Comp_Str == 'TEX' )
//			Shell_to_Dos('COPY ' + truncate_path(truncate_extension(file_name)) + '.LOG ' + user_id + 'MEERR.TMP > NUL',True);
			Shell_to_Dos('COPY ' + truncate_path(truncate_extension(file_name)) + '.LOG ' + e_f_name + ' > NUL',True);

		if ( !Switch_Win_Id(meerr_id) )
		{
			if ( File_Exists(e_f_name) )
			{
				RM('CreateWindow');
				meerr_id = Window_Id;
				Set_Global_Int('~MEERR_ID',meerr_id);
				RM('SetWindowNames');
			}
			else
				meerr_id = 0;
		}

		// Load in the error file
		if ( meerr_id )
		{
			Load_File(e_f_name);
			tel=Error_Level;
			RM('EXTSETUP');
			Set_Global_Int('~MEERR_ID', meerr_id );
		}

		if ( Dont_Process ) {
			Switch_Win_Id(Temp_Id);
//      rm('AutoArrangErrSrc');
      AUTOARRANGERRSRC("");
			Goto EXIT;
		}

		if ( (COMP_STR == 'NOT SUPPORTED') || (COMP_STR == '') ) {
			Switch_Win_Id(Temp_Id);
//      rm('AutoArrangErrSrc M=Unsupported program type.  Check this window for errors.');
      AUTOARRANGERRSRC("Unsupported program type.  Check this window for errors.");
			Switch_Win_Id(MeErr_Id);
			Goto EXIT;
		}

//  SPECIAL PROCESSING FOR DATAFLEX
/* THIS CODE ASSUMES THE USE OF THE FOLLOWING BATCH FILE(%4 is MEERR file):
ECHO OFF
ERASE %4
DFCOMP %1.%2 %3 > Nul:
COPY %1.PRN %4 > Nul:
IF %3 == -V99F GOTO DONE
ERASE %1.PRN
:DONE
EXIT
*/
		// note that the filename and extension must be given as 2 seperate parms
		SET_GLOBAL_INT('FLEX',0);
		// I need to know in cmperror if I have the first or subsequent call
		if ( COMP_STR == 'DATAFLEX'  )
		{
			EOF;
			JX = SEARCH_BWD('ERRORS: 0',0);
			TOF;
			if ( JX == 1 )
			{
				delete_window;
				Switch_Win_Id( temp_id );
				MAKE_MESSAGE('NO ERRORS');
				GOTO EXIT;
			}
		}
		//  END OF DATAFLEX

		// SPECIAL PROCESSING FOR ALSYS, CLARION and INTERMETRICS C
		// Note that entire scheme for doing this compile relies on the
		// fact that DOS will not copy a zero length file.  Thus the bat
		/* file ADACOMP.BAT (%2 is the name of the MEERR file):
ERASE %2
ada comp %1.ADA
COPY %1.LST %2
EXIT
			 will produce a zero length .lst file upon successfull compile */
		//  and MEERR.TMP this is also true for Clarion and Intermetrics C

		if ( ((Comp_Str == 'CLARION')
				|| (Comp_Str == 'ALSYS')
				|| (Comp_Str == 'INTERMETRICS C'))
				&& (tel != 0) )
		{
			if ( meerr_id )
				rm('WinOp /T=1');
			MAKE_MESSAGE('NO ERRORS');
			GOTO EXIT;
		}
		//  End of ALSYS, CLARION and INTERMETRICS C

		// Special processing for the MULTI_EDIT macro language compiler
		if ( Comp_Str == 'MULTI_EDIT' )
		{
			// If no errors then load the macro file
			Ignore_Case = true;
			if ( Search_Fwd('OUTPUT-FILE',0)  ) {
				goto_col(c_col + 12);
				while(  cur_char == ' ' )
					right;
				if ( cur_char == '=' )
					right;

				Comp_Str = Remove_Space(Get_Word(''));
				goto_col(1);

				Load_Macro_File(Comp_Str);
				Switch_Win_Id(temp_id);
//        rm("AutoArrangErrSrc");
        AUTOARRANGERRSRC("");
				Make_Message('No errors.  Macro file = ' + Comp_Str);
				Goto Exit;
			}
		}

		TOF;
		switch_win_id(temp_id);
		// Run the find error macro
		rm('CMPERROR');
	}
	else
		GOTO COMP_PROMPT;
exit:
	temp_id = window_id;
	working;
	Set_Virtual_Display;
	refresh = true;
	switch_win_id( meerr_id );
	redraw;
	switch_win_id( temp_id );
	redraw;
	Update_Virtual_Display;
	Reset_Virtual_Display;
}

macro CMPERROR TRANS {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: CMPERROR

Description:  Finds the next error in the MEERR.TMP window generated by the
	last compile.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/

	str m_str[128],file_str[128],comp_str[128],e_word,l_str[128],svword[128], nxt[128],t_str;  // note: last 3 for alsys
	int Temp_window,jx,ec,el,tc,tl,ofs_stat,vret,scol,DCNT;
	 // note: last 3 for alsys

	Refresh = False;
	Temp_Window = Window_Id;
	Error_Level = 0;
	Ofs_Stat = 0;  /* This variable decides whether or not ec is an absolute colum
										position, or an offset from the first word */

	//  I have changed this macro quite a bit to handle intel languages
	/*  instead of one section which the error is found in the are a couple
		based on compiler type  */

		// Find the error window
	jx = 0;
	while(  (jx < window_count) & (Caps(Truncate_Path(File_Name)) != (user_id + 'MEERR.TMP')) ) {
		jx = jx + 1;
		Switch_Window(cur_window + 1);
	 }
		// If not found then exit
	if ( Caps(Truncate_Path(File_Name) ) != (user_id + 'MEERR.TMP') ) {
		M_Str = "No error file loaded.";
		Switch_Win_Id(Temp_Window);
		Goto Exit2;
	 }

	Ignore_Case = True;
	Reg_Exp_Stat = True;

	m_str = '';
	ec = 1; el = 1;

	// Get the compiler type for the last compile
	Comp_Str = Caps(Global_Str('LAST_COMP'));

	if ( (Comp_Str == 'TURBO C') |              //  Support for Turbo C 1.0 to 2.0
			 (Comp_Str == 'TURBO C++') |            //  as well as Turbo C++ 1.0 and
			 (Comp_Str == 'BORLAND C') |            //  Borland C++ 2.0 and Turbo Assembler
			 (Comp_Str == 'BORLAND C++') |
			 (Comp_Str == 'TURBO ASSEMBLER') )
		RM('ERR_BORLAND');
	else if ( (Comp_Str == 'MULTI_EDIT') ||
						(Copy(Comp_Str, 1, 12 ) == 'TURBO PASCAL') )
		RM('ERR_TURBO_PASCAL /CS=' + Comp_Str);
	else if ( Comp_Str == 'BATCOM' )
		RM('ERR_BATCOM');
	else if ( Comp_Str == 'POWER C 2.0' )
		RM('ERR_POWER_C20');
	else if ( Comp_Str == 'POWER C' )
		RM('ERR_POWER_C');
	else if ( COMP_STR == 'POWER C 1.2.0' )
		RM('ERR_POWER_C120');
	else if (  (COMP_STR == '2500 AD 68000')  )
		RM('ERR_2500_AD_68');
	else if ( COMP_STR == 'DATABUS' )
		RM('ERR_DATABUS');
	else if ( COMP_STR == 'SALFORD FTN77' )
		RM('ERR_SALFORD_FTN7');
	else if ( COMP_STR == 'GIMPEL PC-LINT' )
		RM('ERR_GIMPEL_LINT');
	else if ( COMP_STR == 'GIMPEL PC-LINT 5' )
		RM('ERR_GIMPEL_LINT5');
	else if ( COMP_STR == 'TELXON TCAL V3.3' )
		RM('ERR_TELXON_TCAL');
	else if ( ('MICROSOFT' == Copy(Comp_Str, 1, 9)) ||
						('CLIPPER 5' == Comp_Str) ||
						('WATCOM C' == Comp_Str) ||
						('ZORTECH C++ 3.0' == Comp_Str) )
		{
			Return_Str = Comp_Str;              /*** This supports CLIPPER 5.0 ***/
			RM('ERR_MICROSOFT /PT='+Comp_Str);
		}
	else if ( ('TEX' == Comp_Str) || ('EMTEX' == Comp_Str) )
		RM('Err_TeX');
	else if ( 'MICRO FOCUS COBOL' == Comp_Str )
		RM('Err_MFCobol');
	else if ( 'MS PASCAL' == Comp_Str )
		RM('Err_MS_PASCAL');
	else if ( ('CLIPPER S87' == Comp_Str) ||
						('FOXBASE' == Comp_Str) )
		RM('ERR_C87_FB');
	else if ( 'REALIA COBOL' == Comp_Str )
		RM('ERR_RealiaCobol');
	else if ( 'CROSSCODE C' == Comp_Str )
		RM('Err_CrossCode_C');
	else if ( ('ARCHIMEDES 8051 C' == Comp_Str) ||
						('Archimedes Z80 Assembler' == Comp_Str) ||
						('Archimedes Z80 C' == Comp_Str) )
		RM('Err_Arch_8051_C');
	else if ( 'AVOCET AVMAC65' == Comp_Str )
		RM('Err_AVMAC65');
	else if ( 'ACU-COBOL' == Comp_Str )
		RM('ERR_ACUCOBOL');
	else if ( 'METAWARE' == Comp_Str )
		RM('ERR_Metaware');
	else if ( 'ASPECT' == Comp_Str )
		RM('ERR_ASPECT');
	else if ( 'SVS FORTRAN' == Comp_Str )
		RM('ERR_SVS_FORTRAN');
	else if ( 'MCC68000' == Comp_Str )
		RM('ERR_MCC68000_C');
	else if ( ('2500 AD C' == Comp_Str) ||
						('2500 AD ASSEMBLER' == Comp_Str) )
		RM('ERR_2500AD_C');
	else if ( 'AMD_HIGHC29K' == Comp_Str )
		RM('ERR_AMD_HIGHC29K');
	else if ( 'TDBS' == Comp_Str )
		RM('ERR_TDBS');
	else if ( 'SASM' == Comp_Str )
		RM('ERR_SASM');
	else if ( 'WATCOM C/C++' == Comp_Str )
		RM('ERR_WATCOM_CPP');
	else if ( 'MERIDIAN ADA' == Comp_Str )
		RM('ERR_MERIDIAN_ADA');
	else if ( 'SOFTOOLS' == Comp_Str )
		RM('ERR_SOFTOOLS');
	else if ( 'TI C' == Comp_Str )
		RM('ERR_TI_C');
	else
		Goto NNM;

	switch ( Return_Int )
	{
		case RC_NoMoreErrors :
				M_Str = "No more errors.";
				switch_win_id(Temp_Window);
				break;

		case RC_NoErrors :
				M_Str = "No errors.";
				switch_win_id(Temp_Window);
				break;

		case RC_DisplayError :
				M_Str = Return_Str;
				RM('MEERROR^Beeps /C=1');
				break;

		case RC_ProblemParsing :
				M_Str = "Problem parsing error file.";
				RM('MEERROR^Beeps /C=2');
				break;

		case RC_CantFind :
				M_Str = "Can't find " + Return_Str;
				RM('MEERROR^Beeps /C=2');
				break;

		case RC_DENoBeep :
				M_Str = Return_Str;
				break;
	}
	Goto Final_Exit;
NNM:

	if ( (Comp_Str == 'LAHEY FORTRAN') ) {
// This based on error output generated by version 4.00
		File_Str = Global_Str('last_compiled_window'); //  in main file
		if ( (Search_fwd ('{FATAL}||{WARNING}||{ABORT}',0)) ) {
			up;
			goto_col(1);
			if ( (Search_fwd('^',1)) ) {
				ec = c_col;
				search_bwd('line ?*:',0);
				word_right;
				val (el,get_word(':'));
				ec = Ec - C_Col;
				Search_fwd ('{FATAL}||{WARNING}||{ABORT}',0);
			} else {
				down;
				ec = 0;
				first_word;
				search_fwd('(',0);
				right;
				e_word = get_word(')');
				if ( (search_fwd('line',2)) ) {
					word_right;
					val(el,get_word(' .&'));
					Search_bwd ('{FATAL}||{WARNING}||{ABORT}',0);
				}
			}
			goto display_error;
		}
		goto no_more_errors;
	}

// SPECIAL PROCESSING FOR DATAFLEX
	if ( (COMP_STR == 'DATAFLEX') )
	{
/*  NOTE THAT THE LINE NUMBERS GIVEN IN THE DATAFLEX ERROR FILE ARE
		MEANINGLESS!!!  THEY REFER TO THE SOURCE FILE WITH MACRO EXPANSIONS.
		SO WE CAN'T USE THE NUMBERS TO SHOW THE ERRORS.

		The DATAFLEX users group says that we must search for a bel ^G character
		before the word ERROR:.
 */
		File_Str = Global_Str('last_compiled_window'); //  in main file
		if ( SEARCH_FWD('%ERROR:',0) )
		{
			MARK_POS;
			M_STR = GET_LINE;
			VRET = 0;
RELOOK:
			UP;
			FIRST_WORD;
			SVWORD = GET_WORD(':');
			if ( SVWORD == 'ERROR:' )
			{
				VRET = 1;
				GOTO RELOOK;      // WE JUST SHOWED THIS MESSAGE
			}
			L_STR = GET_LINE;
			SCOL = XPOS('>',L_STR,1);
			L_STR = STR_DEL(L_STR,1,SCOL);
DEBLANK:
			SVWORD = COPY(L_STR,1,1);
			if ( SVWORD == ' ' )
			{
				L_STR = STR_DEL(L_STR,1,1);
				GOTO DEBLANK;
			}
			GOTO_MARK;
			EOL;
			GOTO DISPLAY_ERROR_FLEX;
		}
			GOTO NO_MORE_ERRORS;
	}
// end DATAFLEX

	// SPECIAL PROCESSING FOR ALSYS ADA VERSION 4.X
	if ( (COMP_STR == 'ALSYS ADA 4.X') )
	{
		File_Str = Global_Str('last_compiled_window'); //  in main file
		if ( SEARCH_FWD('[0-9]   *@*',0) )
		{
			L_STR = GET_LINE;
			L_STR = COPY(L_STR,1,2);
			NXT = REMOVE_SPACE(L_STR);
			DCNT = 0;
			UP;
			UP;
RETRY4:
			L_STR = GET_LINE;
			L_STR = COPY(L_STR,1,9);
			M_STR =  REMOVE_SPACE(L_STR);
			VRET = VAL(EL,M_STR);
			if ( VRET != 0 )
			{
				UP;
				DCNT = DCNT + 1;
				GOTO RETRY4;
			}
			DOWN;
			scol = 0;
			if ( search_fwd(nxt,1) )
			{
				up;
				svword = get_word(' :=()-+;');
				down;
				scol = 1;
			}
			DOWN;
			while(  DCNT > 0 )
			{
				DOWN;
				DCNT = DCNT - 1;
			}
			M_STR = GET_LINE;
			GOTO_COL(80);
			GOTO DISPLAY_ERROR_ADA;
		}
		GOTO NO_MORE_ERRORS;
	}
	//  end alsys 4.x

	// SPECIAL PROCESSING FOR ALSYS ADA
	if ( (COMP_STR == 'ALSYS ADA') )
	{
		File_Str = Global_Str('last_compiled_window'); //  in main file
		if ( SEARCH_FWD('[0-9] : *@*',0) )
		{
			DCNT = 0;
			UP;
			UP;
			UP;
RETRY:
			L_STR = GET_LINE;
			L_STR = COPY(L_STR,1,7);
			M_STR =  REMOVE_SPACE(L_STR);
			VRET = VAL(EL,M_STR);
			if ( VRET != 0 )
			{
				 UP;
				 DCNT = DCNT + 1;
				 GOTO RETRY;
			}
			DOWN;
			scol = 0;
			if ( search_fwd('^',1) )
			{
				up;
				svword = get_word(' :=()-+;');
				down;
				scol = 1;
			}
			DOWN;
			DOWN;
			while(  DCNT > 0 )
			{
				DOWN;
				DCNT = DCNT - 1;
			}
			M_STR = GET_LINE;
			GOTO_COL(80);
			GOTO DISPLAY_ERROR_ADA;
		}
			GOTO NO_MORE_ERRORS;
	}
	//  end alsys

			// SPECIAL PROCESSING FOR CLARION
	if ( (comp_str == 'CLARION') )
	{
		File_Str = Global_Str('last_compiled_window'); //  in main file
		// WE SEE IF WE HAVE FINISHED
		if ( AT_EOF == 1 )
			GOTO NO_MORE_ERRORS;
		//  NEXT WE SEE IF THIS IS A NEW FILE
		if ( SEARCH_FWD('Compiled',1) )
		{
			DOWN;
			VRET = SEARCH_FWD('IN',1);
			RIGHT;
			RIGHT;
			RIGHT;
			M_STR = GET_WORD('.'); //  THIS IS THE FILE THAT PRODUCED ERRORS
			L_STR = CAPS(GET_PATH(FILE_STR));
			File_Str = L_STR+ M_STR + '.CLA';
			DOWN;
			FIRST_WORD;
		}
		//  NOW WE ARE AT AN ERROR LINE
		M_STR = GET_WORD('@');
		RIGHT;
		M_STR = GET_WORD('/');
		VRET = VAL(EL,M_STR);
		RIGHT;
		M_STR = GET_WORD(':');
		VRET = VAL(EC,M_STR);
		RIGHT;
		M_STR = GET_WORD('~');  // TO END OF LINE
		DOWN;
		FIRST_WORD;   // PREPARE FOR NEXT ERROR
		GOTO DISPLAY_ERROR_ADA;
	}
	// END CLARION

	if ( (Comp_Str == 'STONYBROOK MODULA-2') )
	{
		File_Str = Global_Str('last_compiled_window'); //  in main file
		word_right;
		goto_col(1);
		M_Str = get_word(' ');
		if ( (Val(El,M_Str) > 0) )
			goto try_stonybrook_21;
		right;
		M_Str = get_word(' ');
		if ( (Val(Ec,M_Str) > 0) )
			Goto NO_MORE_ERRORS;
		Goto DISPLAY_ERROR;
try_stonybrook_21:
		goto_col(1);
		forward_till( '(' );
		right;
		M_Str = get_word(')');
		if ( (Val(El,M_Str) > 0) )
			Goto NO_MORE_ERRORS;
		forward_till('(');
		right;
		M_Str = get_word(')');
		if ( (Val(Ec,M_Str) > 0) )
			Goto NO_MORE_ERRORS;
		Goto DISPLAY_ERROR;
	}

	if ( (Comp_Str == 'LOGITECH MODULA-2') )
	{
		File_Str = Global_Str('last_compiled_window'); //  in main file
		Tc = C_Col;
		Tl = C_Line;
		Tof;
		if ( Search_Fwd('---- error',0) )
		{
			Goto_Col(Tc);
			Goto_Line(Tl);
M2_LOGI_AGAIN:
			if ( Search_Fwd('^',0) )
			{
				Tc = C_Col;
				Tl = C_Line;
				Ec = Tc - 6;
				Goto_Col(2);
				M_Str = Get_Word(' ');
				if ( Xpos('*****',M_Str,1) != 0 )
				{
					Goto_Col(2);
					while(  (Cur_Char == '*') )
						Up;
					Left;
					while(  (Cur_Char == ' ') )
						Right;
					M_Str = Get_Word(' ');
					Jx = Val(El,M_Str);
					if ( Jx != 0 )
						Goto ERROR_EXIT;
					Goto_Line(Tl);
					Goto_Col(Tc);
					Right;
					Right;
					M_Str = Get_Word(', ');
					if ( Search_Fwd(M_Str,0) )
						M_Str = Get_Line;
					else
						Goto ERROR_EXIT;
					Goto_Line(Tl);
					Eol;
					Goto DISPLAY_ERROR;
				}
				else
				{
					Down;
					Goto_Col(1);
					Goto M2_LOGI_AGAIN;
				}
			}
			else
				GOTO NO_MORE_ERRORS;
		}
		else
		{
			Ignore_Case = False;
			if ( Search_Fwd('{----}||{===>}',0) )
			{
				Ignore_Case = True;
				M_Str = '';
				Tof;
				Goto DISPLAY_ER3;
			}
			else
			{
				Ignore_Case = True;
				Goto NO_ERRORS;
			}
		}
	}

	if ( (Comp_Str == 'JPI MODULA-2') )
	{
		Tc = C_Col;
		Tl = C_Line;
		Goto_Col(1);
		if ( Search_Fwd('No Errors',0) )
		{
			Goto_Line(Tl);
			Goto_Col(Tc);
			Goto NO_ERRORS;
		}
		else
		{
			Goto_Line(Tl);
			Goto_Col(Tc);
M2_JPI_AGAIN:
			if ( Search_Fwd('(',0) )
			{
				if ( C_Col != 1 )
				{
					Right;
					Goto M2_JPI_AGAIN;
				}
				Right;
				File_Str = Get_Word(' ');
				while(  Cur_Char == ' ' )
					Right;
				M_Str = Get_Word(',');
				if ( Val(El,M_Str) != 0 )
					Goto ERROR_EXIT;
				Right;
				M_Str = Get_Word(')');
				if ( Val(Ec,M_Str) != 0 )
					Goto ERROR_EXIT;
				Goto DISPLAY_ERROR;
			}
			else
				Goto NO_MORE_ERRORS;
		}
	}

	if ( (COMP_STR == 'JANUS ADA') )
	{
		if ( Search_FWD( '%In File ',0) )
		{
			Goto_Col(9);
			File_Str = Get_Word(' ');
			Word_Right;
			Word_Right;
			Word_Right;
			M_Str = Get_Word('');
			if ( Val(EL,M_Str) != 0 )
				GOTO error_exit;
			Down;
			Down;
			Down;
			Down;
			Down;
			ec = 1;
			goto display_error;
		}
	}


	if ( (Comp_Str=='ZORTECH C++')||('ZORTECH C++ 2.0'==Comp_Str) ) {
		if ( search_fwd('%"?+", line [0-9]', 0 ) ) {
			goto_col(1);
			right;
			file_str = Caps(get_word('"'));
			m_str = get_word('0123456789');
			m_str = remove_space( get_word(' ') );
			if ( Val(EL,M_Str) != 0 ) {
				GOTO error_exit;
			}
			up;
			goto_col(1);
			if ( search_fwd('^',1) ) {
				ec = c_col;
			}
			down;
			eol;
			goto display_error;
		}
	}

	if( (COMP_STR == 'INTEL PLM-86') |
			(COMP_STR == 'INTEL ASM-86') |
			(COMP_STR == 'INTEL IC-86') ) {

		if ( Search_FWD('FATAL ',0) ) {
			RM('MEERROR^Beeps /C=2');
			M_Str = "FATAL COMPILER ERROR";
			goto Exit2;
		}

		if ( Search_FWD('I/O ERROR',0) ) {
			RM('MEERROR^Beeps /C=2');
			M_Str = "COMPILER I/O ERROR";
			goto Exit2;
		}

		if ( SEARCH_FWD('{@*@*@* ERROR}||{@*@*@* WARNING}',0) ) {
			goto_col(1);
			if ( (Comp_Str == 'INTEL PLM-86') ) {
				if ( Search_Fwd('{(}',1) ) {
					Right;
					M_Str = Get_Word(',');
					if ( POS(')',M_Str) == 0 ) {
						File_str = M_Str;   //  error is in an include file
						if ( Search_Fwd('{LINE }',1) == 0 ) {
							goto error_no_line;
						}
					} else {
						if ( Search_Bwd('{LINE }',1) == 0 ) {
							goto error_no_line;
						}
						File_Str = Global_Str('last_compiled_window'); //  in main file
					}
					Word_Right;
					M_Str = Get_Word(')');
					if ( Val(EL,M_Str) != 0 ) {
						goto error_exit;
					}
				} else {
					goto error_no_line;
				}
			}

			if ( (Comp_Str == 'INTEL ASM-86') ) {
				File_Str = Global_Str('Last_Compiled_Window');
				if ( Search_Fwd('{IN }',1) ) {
					Word_Right;
					M_Str = Get_Word(',');
					if ( Val(EL,M_Str) != 0 ) {
						GOTO error_exit;
					}
				} else {
					GOTO error_no_line;
				}
			}

			if ( (Comp_Str == 'INTEL IC-86') ) {
				if ( Search_Fwd('{LINE }',1) ) {
					Word_Right;
					M_Str = Get_Word(' ');
					if ( Val(EL,M_Str) != 0 ) {
						goto error_exit;
					}
					Word_Right;
					Word_Right;
					File_Str = Get_Word(' ');
					File_Str = Str_Del(File_Str, Length(File_Str), 1);
				} else {
					goto error_no_line;
				}
			}

			//  it drops down to here when a true error
			GOTO DISPLAY_ERROR;

		}

		Goto DISPLAY_LINE;

	}

	// Intermetrics support added 02-14-90 10:42am

	if ( COMP_STR == 'INTERMETRICS C' )
	{
		if ( AT_EOF )
			goto no_more_errors;
		if ( cur_char != ':' )
			goto no_more_errors;
		GOTO_COL(2);
		jx = search_fwd(':',1);
		right;
		File_Str = Caps(Get_Word('.'));
		File_Str = File_Str + Caps(Get_word(':'));
		right;
		M_Str = Caps(Get_Word(':'));

		if ( Val(EL,M_Str) != 0 )
			Goto Error_Exit;

		word_right;
		t_Str = Get_Word('');
		m_str = file_str + ' ' + '(' + str(el) + ')' + ' ' + t_str;
		down;
		goto_col(1);   // for eof check to be reliable you must be at beg of line
		call find_file_window;
		goto_line(el);
		goto display_er3;
	}

	// TI370 ASSEMBLER support
	if ( comp_str == 'TI370 ASSEMBLER' )
	{
		if ( at_eof == 1 )
			goto no_more_errors;
		if ( cur_char != '[' )
			goto no_more_errors;
		goto_col(2);
		File_Str = Caps(get_word(' ]'));
		m_str = get_word('0123456789');
		m_str = caps(get_word(' '));
		if ( val(EL, m_str) != 0 )
			goto error_exit;
		right;
		right;
		right;
		t_str = get_word('');
		m_str = file_str + ' ' + '(' + str(el) + ')' + ' ' + t_str;
		down;
		goto_col(1);
		call find_file_window;
		goto_line(el);
		goto display_er3;
	}



	//  all other compilers
	// Search for error or warning messages
DO_AGAIN:
	if ( Search_FWD('{ERROR[~S]}||{WARNING[~S]}||{NOT ENOUGH MEMORY}||{ABORTED}||{TOO BIG}',0) )
	{
		tc = c_col;
		Goto_Col(1);

		if ( Search_FWD('[0-9] Warning Errors',1) ) {
			EOL;
			GOTO Do_AGAIN;
		}

		Goto_Col(1);
		if ( Caps(GET_WORD(' :(')) == 'LINK' ) {
			Goto_Col(1);
			M_Str = Get_Word('');
			Goto DISPLAY_ER3;
		}
		Goto_Col(1);

		if ( (COMP_STR == 'LATTICE C 3.0') ) {
			File_Str = Caps(Remove_Space(Get_Word(' ')));
			Right;
			M_Str = Remove_Space(Get_Word(' '));
			if ( Val(EL,M_Str) != 0 )
				Goto Error_Exit;
		}

		if ( (COMP_STR == 'AZTEC C') ) {
			goto_col(tC);
			if ( Search_BWD(':',1) ) {
				left;
				if ( Search_BWD(':',1) ) {
					EC = C_Col;
					if ( Search_BWD('/',1) ) {
						File_Str = Copy(Get_Line,1,C_Col - 1) + '\' +
								Copy(Get_Line, C_Col + 1, ec - c_col - 1);
					} else {
						File_Str = Copy(Get_Line,1,EC - 1);
							 }
					File_Str = Fexpand(File_Str);
					goto_col(EC);
					right;
					m_str = get_word(':');
					if ( Val(EL,M_Str) != 0 ) {
						Goto Error_Exit;
							 }
					Up;
					goto_col(1);
					if ( search_fwd('^',1) ) {
						ec = c_col;
							 }
					down;
					goto display_error;
						}
				 }
			}

		if ( (COMP_STR == 'RBASE_5') ) {
			Goto_Col(1);
			File_Str = Caps(Remove_Space(Get_Word(' ')));
			Right; Right;
			M_Str = Remove_Space(Get_Word(')'));
			Down;
			if ( Val(EL,M_Str) != 0 )
				Goto Error_Exit;
		}



DISPLAY_ERROR:
		if ( Comp_Str != 'LOGITECH MODULA-2' ) {
			 goto_Col(1);
			 M_Str = Get_Word('');
		}

Display_Er1:
 CALL FIND_FILE_WINDOW;

DISPLAY_ER2:
		while(  C_Row < ((Win_Y2 - Win_Y1) / 2) )
			down;

		goto_line(el - (format_stat != 0));

		if ( ofs_stat )
		{
			first_word;
			ec = c_col + ec;
		}
		else
		{
			COMP_STR = GET_LINE;
			JX = 1;
				/* we need to figure out the actual column number with tab expansion
				included */
			CRUNCH_TABS( COMP_STR, JX );
			EXPAND_TABS( COMP_STR, EC );
		}
		goto_col(ec);

DISPLAY_ER3:
		RM('MEERROR^Beeps /C=1');
		GOTO Exit2;
	}


DISPLAY_LINE:
		EOF;
		Left;
		M_Str = Get_Line;
		switch_win_id(Temp_Window);
		goto exit2;

// FOR DATAFLEX
 DISPLAY_ERROR_FLEX:
			CALL FIND_FILE_WINDOW;
			DCNT = GLOBAL_INT('FLEX');
			if ( DCNT == 0 ) {
				TOF;
				SET_GLOBAL_INT('FLEX',1);  // MUST KNOW IF FIRST ERROR OR LATER
				 }
			if ( VRET == 0 ) {
				JX = SEARCH_FWD(L_STR,0);
				 }
			DCNT = C_LINE;
			GOTO EXIT2;
 //  end DATAFLEX


//  for alsys
 DISPLAY_ERROR_ADA:
			CALL FIND_FILE_WINDOW;
			if ( VRET == 0 ) {
				GOTO_LINE(EL);
				if ( SCOL == 1 ) {
					FIRST_WORD;
				reg_exp_stat = false;
				SCOL = SEARCH_FWD(SVWORD,1);
				reg_exp_stat = true;
						}
				 }
			GOTO EXIT2;
 //  end alsys


NO_MORE_ERRORS:
	M_Str = "No more errors.";
	switch_win_id(Temp_Window);
	GOTO EXIT2;

NO_ERRORS:
	M_Str = "No Errors.";
	switch_win_id(Temp_Window);
	goto exit2;

ERROR_EXIT:
	M_Str = "Problem parsing error file.";
	GOTO Exit2;

ERROR_NO_LINE:
	M_Str = "Error.";
	RM('MEERROR^Beeps /C=1');
	GOTO Exit2;

EXIT2:
	Refresh = true;
	redraw;
GOTO FINAL_EXIT;
//  ========================================================================
FIND_FILE_WINDOW:
		jx = 0;
		while(  (jx <= window_count) & (Caps(Truncate_Path(File_Name)) != Caps(Truncate_Path(File_Str))) ) {
			jx = jx + 1;
			Switch_Window(jx);
			}
		if ( (jx >= Window_Count) & (Caps(Truncate_Path(File_Name)) != Caps(Truncate_Path(File_Str))) ) {
			Switch_Window(Window_Count);
			Create_Window;
			Load_File(file_str);
			RM('SetWindowNames');
			RM('EXTSETUP');
			}
		window_attr = 0;
RET;
FINAL_EXIT:
//  RM("AutoArrangErrSrc M="+M_Str);
  AUTOARRANGERRSRC(m_str);
}

macro ERR_Find_Error {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: ERR_Find_Error

Description:  Brings up the specified file, and moves the cursor to the passed
							line and column.

Parameters: /F=str    filename
						/L=int    line number
						/C=int    Column position
						/OF=1     Use /C as an offset from the first word of the line
											instead of as an absolute position.
						/NL=1			No_Load if the file is not already loaded don't try to load

Returns:    Return_Int == RC_DisplayError if successfull
						Return_Int == RC_CantFind if file could not be found

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	int offset, ec, jx, el;
	str TStr;

	Error_Level=0;
	Return_Str = FExpand(Parse_Str('/F=',MParm_Str));
	if (Switch_File(Return_Str)) {
		if (Window_Attr & 0x40)
			RM('WINDOW^ZOOM');
	} else {
		if (parse_int("/NL=",Mparm_Str))
			goto NO_LOAD;
		rm('LDFILES /CW=1/NW=1');
		if (error_level) {
NO_LOAD:
			Return_Int = RC_CantFind;
			Goto Exit;
		}
	}
	while ( !AT_EOF && (C_Row<((Win_Y2-Win_Y1)/2)) )
		Down;
	el = Parse_Int('/L=',Mparm_Str );
	offset = Parse_Int('/OF=',Mparm_Str );
	ec = Parse_Int('/C=',Mparm_Str );

	if ( el )
		Goto_Line(el-(Format_Stat!=0));

	if (offset)  {
		First_Word;
		ec = C_Col + ec;
	}	else {
		TStr = Get_Line;
		jx = 1;
		// we need to figure out the actual column number with tab expansion included
		Crunch_Tabs(TStr,jx);
		Expand_Tabs(TStr,ec);
	}
	Goto_Col(ec);
	Return_Int = RC_DisplayError;
Exit:
}

macro ERR_TURBO_PASCAL TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_TURBO_PASCAL

Description:  Error file handling for all versions 4 - 6 of Turbo Pascal.
							Also handles Multi-Edit MACRO language.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str Comp_Str[40] = parse_str('/CS=', mparm_str ),
			file_str[80],
			mstr
			;

	int el, ec, ofs_stat = 0;

	return_int = RC_ProblemParsing;
again:
	if ( Search_FWD('{ERROR[~S]}||{WARNING[~S]}||{NOT ENOUGH MEMORY}||{ABORTED}||{TOO BIG}',0) ) {
// This will prevent finding any of the above keywords imbedded in a file name
		if (c_col == 1) {
			eol;
			goto AGAIN;
		}
		Left;
		if ( (Cur_Char != ' ') ) {
			Right;
			Right;
			Goto AGAIN;
		}
		Right;

		Goto_Col(1);
		if ( (COMP_STR == 'MULTI_EDIT') ) {
			if ( (Search_Fwd('{Runtime error}||{too big}',0)) ) {
				if (Search_Fwd('Runtime error found', 0) )  {
					Goto_Col(1);
				} else {
					mstr = "Fatal compiler error probably due to running out of memory.";
					Goto EXIT;
					return_int = RC_DisplayError;
				}
			}
		}
		File_Str = Caps(Remove_Space(Get_Word(' (,')));
		Right;
		mstr = Remove_Space(Get_Word(',)'));
		if ( (File_Str == 'ERROR') ) {
			if ( (Caps(Copy(mstr,1,7)) == 'OPENING') ) {
				Tof;
				mstr = 'Error opening file.';
				return_int = RC_DisplayError;
				Goto EXIT;
			}
		}
		if ( Val(EL,mstr) != 0 ) {
			Goto Error_Exit;
		}
		if ( cur_char == ',' ) {
			right;
			mstr = Remove_Space(Get_Word(')'));
			if ( Val(EC,mstr) != 0 ) {
				Goto Error_Exit;
			}
		}
		word_right;
		mstr = Get_Word('');
		if ( Copy(Comp_Str, 1, 12 ) == 'TURBO PASCAL' ) {
			down;
			first_word;
			ec = c_col;
			ofs_stat = 1;
			down;
			while(  (Cur_Char != '^') && !At_EOL ) {
				right;
			}
			if ( (Cur_Char == '^') ) {
				EC = C_Col - ec;
			}
			UP;
			UP;
		}

		rm('ERR_FIND_ERROR /F=' + file_str + '/L=' + str(el) + '/C=' + str(ec) + '/OF=' + str(ofs_stat ) );
	}
	else
		return_int = RC_NoErrors;
error_exit:
exit:
	return_str = mstr;
}

macro ERR_BORLAND TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_BORLAND

Description:  Error file handling for all versions of Turbo C, including
					 :  Turbo C++ 1.0 and Borland C++ 2.0.  Turbo Assembler error
					 :  file handling added 04-29-91.  TLINK errors improved
					 :  11-05-91.  Borland Make and PolyMake errors added 01-03-92

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str  FName[80], T_Str[80], M_Str;
	int  ln, e, jx ,tw = Window_Id;

	Return_Int  = FALSE;
	Ignore_Case = True;
Do_More:
	if ( Search_Fwd( '{%@*{@*Fatal@*}||{@*Error@*}||{Warning}@*}||{%{fatal}||'+
		'{error}||{warning}[ :]}||{Not enough memory}||{make:}||{@*@* error}', 0) )
	{
		jx = 1;
		ln = 1;
		T_Str = '';
		if ( (copy(Found_Str, length(Found_Str), 1) == ':') ||
			 (caps(Found_Str) == 'NOT ENOUGH MEMORY') ||
			 (caps(Found_Str) == '** ERROR') )
		{
			M_Str = Get_Line;
			Eol;
			rm('ERR_Find_Error /F='+Global_Str('Last_Compiled_Window'));
			if ( RC_CantFind == Return_Int )
				M_Str = Return_Str;
			Goto done;
		}

		M_Str = Get_Word(' ');
		Right;
		FName = Caps(Get_Word(' ('));

		if ( ( Copy(FName, Length(FName), 1) == ':' ) )
		{                                                  /*  For Turbo Assembler:  */
			Eol;                                             /*  'Error messages:   4' */
			Goto Do_More;                                    /*  'Warning message:  2' */
		}

		FName = FExpand(FName);
		Right;

		if ( VAL( ln, Get_Word(': )') ) == 0  )
		{
			Right;
			jx = c_col;
			if ( Search_Fwd("'", 1)  )
			{
				Right;
				T_Str = Get_Word("'");
			}
			M_Str = M_Str + ': ' + copy(Get_Line,jx,132);
		}
		else
		{
			Return_Int = RC_ProblemParsing;
			Goto Exit;
		}

		eol;

		rm('ERR_Find_Error /F=' + fname + '/OF=1/C=0/L=' + str(ln) );
		if( return_int == RC_DisplayError )
		{
			if ( T_Str != ''  )
			{
				reg_exp_stat = FALSE;
				Search_Fwd( T_Str, 1 );
				reg_exp_stat = TRUE;
			}
		}
		else
			M_Str = Return_Str;

	DONE:
			Return_Str = M_Str;
	}
	else
	{
			Return_Int = RC_NoMoreErrors;
			Tof;
	}
Exit:
} /* ERR_BORLAND */

macro ERR_BATCOM TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_BATCOM

Description:  Error file handling for BATCOM batch file compiler

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str  fname[100] = Global_Str('last_compiled_window'),
					tstr[80], mstr;
	int  ln, e, jx ;

	Return_Int = False;
	if ( search_fwd( '%{@*@*@* ERROR @*@*@*}||{Compile is not complete.}', 0) )
	{
		MSTR = Get_line;
		if ( (Found_Str == '*** ERROR ***')  )
			MSTR = Copy(Mstr,15,132);
		else
		{
			Mark_Pos;
			Tof;
			if ( (Search_Fwd('LINK',0))  )
				Mstr = Mstr + '  Check libraries and external object files.';
			else
				Mstr = Mstr + '  Probably due to linker not found.';
			Goto_Mark;
		}
		Eol;
		if ( (MStr == 'Can not find input file.') )
			Goto DONE;
		Up;
		TStr = Get_Line;
		Down;
/* This one does not specify line numbers in the listing, so we have to
search for the line.  This method has many drawbacks, not the least of which is
identical lines. */

		rm('ERR_Find_Error /NL=1/F=' + fname);
		if (return_int == RC_CantFind) {
			Return_Str = FName;
			goto EXIT;
		}
		Tof;
		Search_Fwd('%' + TStr + '$',0);
		Return_Int = True;

DONE:
		Return_Int = RC_DisplayError;
		Return_Str = MStr;
	}
	else
		Return_Int = RC_NoMoreErrors;
EXIT:
} // ERR_BATCOM

macro ERR_POWER_C20 TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_POWER_C20

Description:  Error file handling for Power C 2.0 compiler

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str fname[100] = Global_Str('last_compiled_window'),
			mstr;
	int ln, e, jx ;

	Return_Int = False;
	if ( search_fwd( '%Line', 0)  )
	{
		Forward_Till('0123456789');
		if ( (Val(ln,Get_Word(':')))  )
		{
			Return_Int = RC_ProblemParsing;
			goto EXIT;
		}
		else
		{
			Right;
			Right;
			Mstr = Get_Word('');
			rm('ERR_Find_Error /NL=1/F=' + fname +
				"/L=" + str(ln));
			if (return_int == RC_CantFind) {
				Return_Str = FName;
				goto EXIT;
			}
			Return_Int = True;
		}

		Return_Str = MStr;
	}
	else
		Return_Int = RC_NoMoreErrors;
EXIT:
} // ERR_POWER_C20

macro ERR_POWER_C TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_POWER_C

Description:  Error file handling for Power C 1.0 compiler

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str fname[100] = Global_Str('last_compiled_window'),
			mstr;
	int el, ec;

	Return_Int = False;
	if ( Search_FWD( 'ERROR IN FILE:',0)  )
	{
		MStr = Get_Word(':');
		right;
		Fname = Get_Word('');
		up;
		up;
		goto_col(1);
		if ( Val(EL,REMOVE_SPACE(Get_Word('||'))) != 0  )
		{
			Return_Int = RC_ProblemParsing;
			goto EXIT;
		}
		down;
		goto_col(1);
		if ( Search_Fwd('^', 1)  )
			ec = C_Col - 6;
		down;
		down;
		rm('ERR_Find_Error /NL=1/F=' + fname +
				"/L=" + str(el) +
				"/C=" + str(ec));
		if (return_int == RC_CantFind) {
			Return_Str = FName;
			goto EXIT;
		}
		Return_Str = MStr;
	}
	else
		Return_Int = RC_NoMoreErrors;
EXIT:
} // ERR_POWER_C

macro ERR_POWER_C120 TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_POWER_C120

Description:  Error file handling for Power C 1.2.0 and 2.2.0 compiler

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	int Ln,Cn=C_Col,Co;
	str Em,Fn[128];
	if ( Search_Fwd("%{[ \x9]+[0-9]+:}",0) )
	{
		Em = Remove_Space(Get_Line);
		Eol;
		Mark_Pos;
		if ( Search_Bwd('%?+([0-9]+):',0) )
		{
			Fn = Get_Word('(');
			Right;
			if ( !Val(Ln,Get_Word(')')) )
			{
				Down;
				Goto_Col(1);
				Forward_Till_Not('*');
				Co = C_Col;
				if ( Search_Fwd("^",1) )
					Cn = C_Col - Co;
				Goto_Mark;
				RM('Err_Find_Error /F='+Fn+'/L='+Str(Ln)+'/C='+Str(Cn));
				if ( RC_DisplayError == Return_Int )
					Return_Str = Em;
				else
					Return_Str = Fn;
			}
			else
			{
				Goto_Mark;
				Return_Int = RC_ProblemParsing;
			}
		}
		else
		{
			Pop_Mark;
			Return_Int = RC_ProblemParsing;
		}
	}
	else
		Return_Int = RC_NoMoreErrors;
} // ERR_POWER_C120

macro ERR_2500_AD_68 TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_2500_AD_68

Description:  Error file handling for 2500 AD 68000 assembler

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str fname[100] = Global_Str('last_compiled_window'),
			mstr;
	int ln ;

	Return_Int = False;
	if ( (At_Eof)  )
		Return_Int = RC_NoMoreErrors;
	else
	{
		Goto_Col(4);
		if( (Val(ln,Get_Word(' '))) )
		{
			Return_Int = RC_ProblemParsing;
			goto EXIT;
		}
		else
		{
			Goto_Col(49);
			Mstr = Get_Word('');
			Down;
			MStr = MStr + '  ' + Remove_Space(Get_Line);
			Down;
			rm('ERR_Find_Error /NL=1/F=' + fname +
					"/L=" + str(ln));
			if (return_int == RC_CantFind) {
				Return_Str = FName;
				goto EXIT;
			}
//			Return_Int = True;
		}

//DONE:
//		Return_Int = RC_DisplayError;
		Return_Str = MStr;
	 }
EXIT:
} // ERR_2500_AD_68

macro ERR_DATABUS TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_DATABUS

Description:  Error file handling DATABUS

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str fname[100] = Global_Str('last_compiled_window'),
			mstr;
	int ln, col ;

	Return_Int = False;
	if ( (Search_Fwd('@*$',0))  )
	{
		Col = C_Col;
		Down;
		MStr = Remove_Space(Get_Line);
		Up; Up;
		Goto_Col(1);
		if ( (Val(ln,Remove_Space(Get_Word('.'))))  )
		{
			Return_Int = RC_ProblemParsing;
			goto EXIT;
		}
		Col = Col - 34;
		Down;
		Eol;
		rm('ERR_Find_Error /NL=1/F=' + fname +
				"/L=" + str(ln) +
				"/C=" + str(col));
		if (return_int == RC_CantFind) {
			Return_Str = FName;
			goto EXIT;
		}
		Return_Str = MStr;
	}
	else
		Return_Int = RC_NoMoreErrors;
EXIT:
} // ERR_DATABUS

macro ERR_SALFORD_FTN7 TRANS2 {
/*******************************************************************************
													 MULTI-EDIT MACRO

Name:  ERR_SALFORD_FTN7

Description:  Error file handling for Salford Fortran 77/386

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str fname[100] = Global_Str('last_compiled_window'),
			mstr;
	int ln ;

	Return_Int = False;
	if ( (Search_Fwd('%????)',0)) )
	{
		if ( (Val(ln,Remove_Space(Get_Word(')')))) )
		{
			Return_Int = RC_ProblemParsing;
			goto EXIT;
		}
		Down;
		MStr = Remove_Space(Get_Line);
		rm('ERR_Find_Error /NL=1/F=' + fname +
				"/L=" + str(ln));
		if (return_int == RC_CantFind) {
			Return_Str = FName;
			goto EXIT;
		}
		Return_Str = MStr;
	}
	else
		Return_Int = RC_NoMoreErrors;
EXIT:
} // ERR_SALFORD_FTN7

macro ERR_GIMPEL_LINT TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_GIMPEL_LINT

Description:  Error handling for PC-lint.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str fname[100] = Global_Str('last_compiled_window'),
			mstr;
	int ln ;

	Return_Int = False;
	if ( (Search_Fwd(' : {WARNING}||{INFO}',0)) )
	{
		Goto_Col(C_Col + 3);
		MStr = Remove_Space(Copy(Get_Line,C_Col,132));
		Goto_Col(1);
		Forward_Till('(');
		Right;
		if ( (Val(ln,Remove_Space(Get_Word(')')))) )
		{
			Return_Int = RC_ProblemParsing;
			goto EXIT;
		}
		Down;
		rm('ERR_Find_Error /NL=1/F=' + fname +
				"/L=" + str(ln));
		if (return_int == RC_CantFind) {
			Return_Str = FName;
			goto EXIT;
		}
		Return_Str = MStr;
	}
	else
		Return_Int = RC_NoMoreErrors;
EXIT:
} // ERR_GIMPEL_LINT

macro Err_Gimpel_Lint5 Trans2
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_GIMPEL_LINT5

Description:  Error handling for PC-lint v5.x

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
{
	Str Fn[128] = Global_Str('Last_Compiled_Window'), Es, Tmp_Str;
	int Ln;


	//if ( Search_Fwd('%{File ?*}||{?+  [0-9]}||{Warning [0-9]+:}||{Error [0-9]+:}||{Info [0-9]+:}',0) )
	if ( Search_Fwd('%{File ?+{line}}||{Warning [0-9]+:}||{Error [0-9]+:}||{Info [0-9]+:}||{[~ ]+  [0-9]+  w||i||e}',0) )
	{
		Tmp_Str = Get_Word(' ');
		if ( Tmp_Str == 'File' )
		{
			Forward_Till(' ');
			Right;
			Fn=Get_Word(',');
			Forward_Till('0123456789');
			if ( !Val(Ln,Get_Word('')) )
			{
				if ( Search_Fwd('% +{Warning}||{Error}||{Info} [0-9]+:',0) )
					Es=Remove_Space(Get_Word(''));
				else
					Return_Int=RC_ProblemParsing;
			}
			else
				Return_Int=RC_ProblemParsing;
		}
//  	else if ( (Tmp_Str != 'Warning')&&(Tmp_Str != 'Error')&&(Tmp_Str != 'Info') )
		else if ( !XPos(''+Tmp_Str+'','WarningErrorInfo',1) )
		{
			Goto_Col(1);
			Fn = Get_Word(' ');
			Search_Fwd(' [0-9]+',0);
			Right;
			if ( Val(ln,Get_Word(' |9')) )
			{
				Return_Int = RC_ProblemParsing;
				goto EXIT;
			}
			Forward_Till(' IWE');
			ES=(Get_Word(''));
		}
		else if ( (Tmp_Str == 'Warning')||(Tmp_Str == 'Error')||(Tmp_Str == 'Info') )
		{
			Search_Fwd(' [0-9]+',0);
			Right;
			if ( Val(ln,Get_Word(':')) )
			{
				Return_Int = RC_ProblemParsing;
				goto EXIT;
			}
			Right;Right;
			ES=Tmp_Str + ': ' + (Get_Word(''));
			//Mark_Pos;
			//Down;
			//Goto_Col(1);
			//if ( (cur_char == ' ')|| (cur_char == '|9'))
			//ES += Remove_Space(Get_word(''));
			//Goto_Mark;
			Mark_Pos;
			if ( Search_Bwd("%{ |\t}+--- Wrap-up",0) )
			{
				Forward_Till(':'); Right; Right;
				Fn = Get_Word('');
			}
			Goto_Mark;
		}
	}
	else
	{
		Return_Int=RC_NoMoreErrors;
		TOF;
	}
	rm('ERR_Find_Error /F='+Fn+'/L='+Str(Ln));
	Return_Str=Es;
EXIT:
}

macro Err_Telxon_TCal Trans2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_TELXON_TCAL

Description:  Error file handling for Telxon TCal Compiler Version 3.3

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str FName[80], MStr;
	int jx, ln, Tw = Window_Id;

	Return_Int = FALSE;
	if ( Search_Fwd('line', 0) )
	{
		MStr  = Get_Line;
		Forward_Till('1234567890');
		if ( ( Val(ln, Remove_Space(Get_Word(' ')))) )
		{
			Return_Int = RC_ProblemParsing;
			Goto Exit;
		}
		if ( ( 1 == Xpos('"', MStr, 1) ) )
		{
			jx = Xpos('",', MStr, 1);
			if ( ( jx > 2 ) )
			{
				FName = Copy(MStr, 2, jx - 2);
				FName = FExpand(FName);
				jx = Xpos(':', MStr, 1);
				if ( ( jx ) )
				{
					jx = 1 + jx;
					MStr  = Str_Del(MStr, 1, jx);
					rm('ERR_Find_Error /F=' + fname +
							"/L=" + str(ln));
/*
					if (switch_file(fname)) {
						if (Window_Attr & 0x40)
							RM('WINDOW^ZOOM');
					} else {
						Switch_Window(Window_Count);
						Create_Window;
						Error_Level = 0;
						Load_File(fname);
						if ( (Error_Level) )
						{
							Delete_Window;
							Switch_Win_Id(TW);
							Return_Int = RC_CantFind;
							Return_Str = FName;
							goto EXIT;
						}
						RM('SetWindowNames');
						RM('EXTSETUP');
					}
					Goto_Line(ln);
					Return_Int = RC_DisplayError;
*/
					Return_Str = MStr;
				}
			}
		}
	}
	else
		Return_Int = RC_NoMoreErrors;
Exit:
} // Err_Telxon_TCal

macro ERR_MICROSOFT TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_MICROSOFT

Description:  Error file handling for Microsoft languages and Clipper v5.0.
							Modifications made to parse Microsoft BASIC which includes column
							information.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str M_Str,F_Str[128],Comp_Str=Parse_Str("/PT=",MParm_Str);
	int jx,Ln,Cn=0,Errors=0,Tw=Window_Id;

	Return_Int = False;
	Ignore_Case = True;

	if ( ( (Comp_Str == 'MICROSOFT C /FS') |
				 (Comp_Str == 'MICROSOFT MASM LIST') ) )
	{
		if ( Search_Fwd('%@*@*@*@*@*',0) )
		{
			Forward_Till(' ');
			Right;
Do_More:
			++Errors;
			F_Str = Get_Word('(');
			Right;
			M_Str=Remove_Space(Get_Word(')'));
			if ( 0!=(jx=XPos(',',M_Str,1)) )
				if ( Val(Cn,Copy(M_Str,jx+1,4)) )
				{
					Return_Int=RC_ProblemParsing;
					Goto Exit;
				}
				else
					M_Str=Copy(M_Str,1,jx-1);
			if ( Val(Ln,M_Str) )
			{
					Return_Int=RC_ProblemParsing;
					Goto Exit;
			}
			Search_Fwd('{:||) +}{fatal}||{error}||{warning}',1)
			Right;
			M_Str = Remove_Space(Get_Word(''));
			Eol;
		}
	}
	else
	{
		if ( Search_Fwd('{:||) +}{fatal}||{error}||{warning}',0) )
		{
			Goto_Col(1);
			Goto Do_More;
		}
	}

//  If 'Errors' is non-zero we have errors. The error message is in 'M_Str' and
//  the line number is in 'Ln'

	if ( Errors ) {
		// We need to change all '/' to '\'. Microsoft C does this when there is
		//  an error in a header file: C:/C600/INCLUDE\sys\stat.h.  Also Microsoft
		//  C accepts include file names with '/' in them (Unix)
		for(;jx=XPos('/',F_Str,1);F_Str=Str_Ins('\',Str_Del(F_Str,jx,1),jx))
				;
		rm('ERR_Find_Error /F=' + f_str +
				"/L=" + str(ln) +
				"/C=" + str(cn));
		if (return_int == RC_CantFind)
			goto EXIT;

/*
		F_Str = FExpand(F_Str);
		if ( !Switch_File(F_Str) )
		{
			Switch_Window(Window_Count);
			Create_Window;
			Error_Level = 0;
			Load_File(F_Str);
			if ( (Error_Level) )
			{
				Delete_Window;
				Switch_Win_Id(TW);
				Return_Int = RC_CantFind;
				Return_Str = F_Str;
				goto Exit;
			}
			RM('SetWindowNames');
			RM('EXTSETUP');
		}
		Goto_Line(Ln);
		if ( Cn )
			Goto_Col(Cn);
		Return_Int = RC_DisplayError;
*/
		Return_Str = M_Str;
	} else {
		if ( 0 != Search_Fwd('%Include file',0) )
		{
			if ( 0 != Search_Fwd('not found',1) )
			{
				++Errors;
				M_Str = Get_Line;
				Eol;
			}
		}

		if ( Errors )
		{
			Return_Int = RC_DisplayError;
			Return_Str = M_Str;
		}
		else
		{
			Return_Int = RC_NoMoreErrors;
			TOF;
		}
	}
Exit:
} // ERR_MICROSOFT

macro Err_TeX TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  Err_TeX

Description:  Error file handling for EmTeX.  In practice, any TeX...

************* (nc) No-Copyright 1990,91 by Small Planet Software ***************/
	str  fname[80], tstr[80], mstr;
	int  TW = Window_Id;
	str  NumStr;
	int  ErrLn, ErrCol, BraceMatch, Done;
	int  CurLine;

	Return_Int = False;

	if ( Search_Fwd('{%!}||{%Overfull \?*[0-9]--[0-9]+}',0) )
	{
		MStr = Get_Line;
		if ( Search_Fwd('%l.[1-9]',0) )
		{
			Goto_Col(3);
			NumStr = Get_Word(' ');
			if ( Val(ErrLn,NumStr) )
			{
				Return_Int = RC_ProblemParsing;
				Goto Exit;
			}
			CurLine = C_Line;   //  save where we are in the error file
			Eol;                //  goto the end of this line
			Word_Left;          //  go back to beginning of token that caused error
			ErrCol = C_Col;     //  this is the column that we want to goto
			Goto_Col(3);        //  but first we have to adjust the ErrCol by the
			Word_Right;         //    length of the first "word" (l.nnn) of the line
			ErrCol = ErrCol - C_Col + 1; //  that reports the error
		}
		else
		{
			if( Copy(MStr,1,10) == 'Overfull \' )
			{
				Eol;
				Word_Left;
				NumStr = Get_Word(' -');
				Val(ErrLn,NumStr);
				CurLine = C_Line;
				ErrCol = 1;
			}
			else
			{
				Return_Int = RC_CantFind;
				Return_Str = '"l.nnn" parsing .log file...';
				Goto Exit;
			}
		}
		//  Now search (backward) for the appropriate filename
		Done = 0;
		BraceMatch = 1;
		while(  !Done && (Search_Bwd('[()]',0))  )
		{
			if(  Found_Str == ')'  )
				BraceMatch = BraceMatch+1;
			if(  Found_Str == '('  )
				BraceMatch = BraceMatch-1;
			Done = !BraceMatch;
			if( !Done )
				Left;
		}

		Right;
		FName = Get_Word(' ');
		Goto_Line(CurLine);       /*  restore our place in the error file so we can
																find the next error  */
		if(  Found_Str != '('  )
		{
			Return_Int = RC_CantFind;
			Return_Str = '''(filename'' parsing .log file...';
			Goto Exit;
		}
		rm('ERR_Find_Error /F=' + fname +
				"/L=" + str(ErrLn) +
				"/C=" + str(ErrCol));
		if (return_int == RC_CantFind)
			goto EXIT;
/*
		FName = Caps(FExpand(FName));

		if ( !Switch_File(FName) )
		{
			Switch_Window(Window_Count);
			Create_Window;
			Error_Level = 0;
			Load_File(fname);
			if ( Error_Level )
			{
				Delete_Window;
				Error_Level = 0;
				Switch_Win_Id(TW);
				Return_Int = RC_CantFind;
				Return_Str = fname;
				Goto Exit;
			}
			RM('SetWindowNames');
			RM('EXTSETUP');
		}
		Goto_Line(ErrLn);
		Goto_Col(1);
		Goto_Col(ErrCol);

		Return_Int = RC_DisplayError;
*/
		Return_Str = MStr;
	}
	else
		Return_Int = RC_NoMoreErrors;
Exit:
} // Err_TeX

macro ERR_MFCOBOL Trans2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_MFCOBOL

Description:  Error file handling for Microsoft/Micro Focus COBOL v4.0

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str Error_Number, Temp_Str;
	int Line_Number;

	if(  ( 0 != Search_Fwd('( +[0-9]+)@*@*', 0) )  )
	{                                                     //   Look for '(   x)**'
		Goto_Col(2);                                        //   Goto second column
		Error_Number = Remove_Space(Get_Word('*'));         //   Get word till '*'
		Up;                                                 //   Go up one line
		First_Word;                                         //   Go to first word
		if(  ( 0 != Val(Line_Number, Get_Word(' ')) )  )
		{                                                   //   Get line number
			Line_Number = -1;                                 //   No line number
		}
		Down;                                               //   Go down 2 lines
		Down;
		Eol;
		if(  ( -1 != Line_Number )  )
		{                                                   //   If line number detected
			Error_Number = Error_Number + ':  ';              //      use a colon
		}
		else
		{                                                   //   Else
			Error_Number = Error_Number + '||  ';             //      use a vertical bar
		}                                                   //   Print error message
		RM('MEERROR^Beeps /C=1');
		Temp_Str = Error_Number + Remove_Space(Copy(Get_Line, 3, 132));
		rm('ERR_Find_Error /NL=1/F=' + Global_Str('last_compiled_window') +
				"/L=" + str(line_number));
/*
		Switch_File(Global_Str('last_compiled_window'));
		if(  ( -1 != Line_Number )  )
		{
			Goto_Line(Line_Number);
		}
*/
		Return_Str = Temp_Str;
//		Return_Int = RC_DisplayError;
	}
	else
		Return_Int = RC_NoMoreErrors;
Exit:
} // ERR_MFCOBOL

macro ERR_MS_PASCAL Trans2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_MS_PASCAL

Description:  Error file handling for Microsoft PASCAL

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	int EL;

	if ( Search_Fwd('@^',0) )
	{
		goto_col(7);
		word_right;
		if ( Val(EL,remove_space(get_word(' '))) != 0 )
			Return_Int = RC_ProblemParsing;
		else
		{
			Return_Str = get_word('');
			Return_Int = RC_DisplayError;
			Switch_File(Global_Str('last_compiled_window'));
			if (Window_Attr & 0x40)
				RM('WINDOW^ZOOM');
			Goto_Line(EL);
		}
	}
	else
		Return_Int = RC_NoMoreErrors;
} // ERR_MS_PASCAL

Macro ERR_C87_FB TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_C87_FB

Description:  Error file handling for Clipper Summer 87 and FoxBase

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str FName[80], M_Str;

	int ErrorWindow = Window_Id,
			LastLine,
			ErrorColumn,
			ErrorLine;

	if ( (Search_Fwd('%{error in }*line [0-9]+:',0)) )
	{
		Forward_Till('0123456789');
		if ( Val(ErrorLine,Remove_Space(Get_Word(':'))) > 0 )
			Return_Int = RC_ProblemParsing;
		else
		{
			LastLine = C_Line;
			Right;
			M_Str = '--' + Get_Word('');
			Down;
			Goto_Col(1);
			if ( Caps(Get_Word(' ')) != 'LINE' )
			{
				down;
				if ( (Search_Fwd('^',1)) )
					ErrorColumn = C_Col;
			}
			Goto_Line(LastLine);
			if ( !Search_Bwd('%COMPILING ',0) )
				Return_Int = RC_ProblemParsing;
			else
			{
				Forward_Till(' ');
				FName = FExpand(Remove_Space(Get_Word('')));
				Goto_Line(LastLine);
				eol;
				rm('ERR_Find_Error /F=' + fname +
						"/L=" + str(ErrorLine) +
						"/C=" + str(ErrorColumn));
				if (return_int == RC_CantFind)
					goto EXIT;
/*
				if (!Switch_File(FName)) {
					Switch_Window(Window_Count);
					Create_Window;
					Error_Level = 0;
					Load_File(FName);

					if ( (Error_Level) )
					{
						Delete_Window;
						Switch_Win_Id(ErrorWindow);
						Return_Int = RC_CantFind;
						Return_Str = FName;
						Error_Level = 0;
						Goto Exit;
					}

					RM('SetWindowNames');
					RM('EXTSETUP');
				}
				Goto_Line(ErrorLine);
				Goto_Col(ErrorColumn);
*/
				Return_Str = M_Str;
//				Return_Int = RC_DisplayError;
			}
		}
	}
	else
		Return_Int = RC_NoMoreErrors;
Exit:
}

macro ERR_RealiaCobol TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_RealiaCobol

Description:  Error file handling for Realia COBOL

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	int ErrorLine;

	if ( (Search_Fwd('%????? E||W',0)) )
	{
		if ( Cur_Char == ' ' )
			Word_Right;
		if ( Val(ErrorLine,Get_Word(' ')) != 0 )
			Return_Int = RC_ProblemParsing;
		else
		{
			Goto_Col(7);
			if ( (Cur_Char == 'E') )
				Return_Str = 'ERROR: ';
			else
				Return_Str = 'WARNING: ';
			Goto_Col(9);
			Switch_File(Global_Str('Last_Compiled_Window'));
			Goto_Line(ErrorLine);
			Return_Str = Return_Str + Remove_Space(Get_Word(''));
			Return_Int = RC_DisplayError;
		}
	}
	else if ( (C_Line == 1) )
		Return_Int = RC_NoErrors;
	else
		Return_Int = RC_NoMoreErrors;
} // ERR_RealiaCobol

/*******************************************************************************
															MULTI-EDIT MACRO

Name:  Err_CrossCode_C

Description:  Error file handling for Cross Code C

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
macro Err_CrossCode_C Trans Trans2 {
	str Fn[80],Es;
	int Ln, Cn;

	if ( Search_Fwd('%{LINKER:}||{?+:"?+"}', 0) ) {
		if ( Copy(Found_Str, 1, 7) != 'LINKER:' ) {
			Search_Fwd('"?+"', 1);
			Fn = Copy(Found_Str, 2, Length(Found_Str) - 2);
			Make_Message(fn);
			Search_Fwd('line [0-9]+', 1);
			Forward_Till('0123456789');
			if ( VAL(Ln, Get_Word(' :')) ) {
				Return_Int = RC_ProblemParsing;
				Goto Exit;
			}
			if ( ':'!=Cur_Char )
			{
				Forward_Till('0123456789');
				if ( VAL(Cn, Get_Word(':')) ) {
					Return_Int = RC_ProblemParsing;
					Goto Exit;
				}
			}
			Right; Right;
			Es = Get_Word('');
			rm('Err_Find_Error /F='+Fn+'/L='+str(Ln)+'/C='+str(Cn));
			if ( RC_DisplayError == Return_Int )
				Return_Str = Es;
			else
				Return_Str = Fn;
		}
		else
		{
			Return_Str = Get_Line;
			Return_Int = RC_DisplayError;
			Eol;
		}
	}
	else
		Return_Int = RC_NoMoreErrors;
Exit:
}

/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_ARCH_8051_C

Description:  Error file handling for Archimedes 8051 C

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
macro Err_Arch_8051_C Trans2 {
	int El, Ec = 0;
	str M_Str, F_Str;

	if ( Search_Fwd('%{"?+",[0-9]+ +{Error}||{Warning}@[[0-9]+@]:}'+
			'||{Error in}||{Error@[[0-9]+@]}||{Errors: +{[0-9]+}||{none}}||{Fatal!}', 0)) {
		if ( 'Error' == Copy(Found_Str,1,5) ) {
			if ( 'Error in' == Found_Str ) {	 // Assembler error
				Forward_Till('01234567890');
				if ( Val(El,Get_Word(':')) ) {
					Return_Int = RC_ProblemParsing;
					Goto Exit;
				}
				Right;Right;
				M_Str = 'Error: '+Get_Word('');
				rm('Err_Find_Error /F='+Global_Str('Last_Compiled_Window')+'/L='+Str(El));
				Return_Str = M_Str;
			}
			else if ( 'Errors' == Copy(Found_Str,1,6) ) {
				M_Str = Get_Word('')+', '; Down; Goto_Col(1);
				M_Str = M_Str+Get_Word('');
				rm('Err_Find_Error /F='+Global_Str('Last_Compiled_Window'));
				Return_Str = M_Str;
				Return_Int = RC_DENoBeep;
			}
			else if ( 'Error[' == Copy(Found_Str,1,6) ) {  // Linker error
				Forward_Till(':');
				Right;Right;
				M_Str = 'Linker: ' +Get_Word('(');
				if ( '(' == Cur_Char )
				{
					Right;Right;
					F_Str = Get_Word(' ');
				}
				else
					F_Str = Global_Str('Last_Compiled_Window');
				rm('Err_Find_Error /F='+F_Str);
				if ( Return_Int == RC_DisplayError )
					Return_Str = M_Str;
			}
		}
		else if ( 'Fatal' == Copy(Found_Str,1,5) ) {
			M_Str = Get_Word('');
			rm('Err_Find_Error /F='+Global_Str('Last_Compiled_Window'));
			Return_Str = M_Str;
			Return_Int = RC_DisplayError;
		}
		else {
			Right;
			F_Str = Get_Word('"');
			Forward_Till('0123456789');
			if ( Val(El,Get_Word(' ')) ) {
				Return_Int = RC_ProblemParsing;
				Goto Exit;
			}
			Forward_Till_Not(' ');
			M_Str = Get_Word('[');
			Forward_Till(':');
			M_Str = M_Str+Get_Word('');
			Goto_Col(1);
			Up;
			if ( Search_Fwd('@^', 1) ) {
				Ec = C_Col;
				Up;
				First_Word;
				Ec = Ec - C_Col;
				Down;
				Down;
			}
			else
					Down;
			Eol;
			rm('Err_Find_Error /OF=1/F='+F_Str+'/C='+Str(Ec)+'/L='+Str(El));
			Return_Str = M_Str;
//			Return_Int = RC_DisplayError;
		}
	}
	else
	{
		Tof;
		Return_Int = RC_NoMoreErrors;
	}
Exit:
}

/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_AVMAC65

Description:  Error file handling for Avocet AVMAC65.  Command line for AVMAC65
							should be "AVMAC65 <FILE> ERRONLY QUIET PR=<MEERR><NR>"

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
macro Err_AVMAC65
{
	str FStr[128],MStr;
	int Ln,LastColon;

	if ( Search_Fwd('%-----> ',0) )
	{
		Forward_Till(' ');
		Right;
		FStr=Get_Word(' ');
		for ( Ln=0;Ln=XPos(':',FStr,1+Ln);LastColon=Ln-1 )
			;
		FStr=FExpand(Copy(FStr,1,LastColon));
		Search_Bwd(':',1);
		Right;
		if ( VAL(Ln,Get_Word(' ')) )
		{
			Return_Int=RC_ProblemParsing;
			Goto Exit;
		}
		Right;
		MStr=Get_Word(' ')+': ';
		Forward_Till_Not(' ');
		Forward_Till(' ');
		Right;
		MStr=MStr+Get_Word('');
		rm('ERR_Find_Error /F='+FStr+'/L='+Str(Ln));
//		Return_Int=RC_DisplayError;
		Return_Str=MStr;
	}
	else
	{
		Return_Int = RC_NoMoreErrors;
		Tof;
	}
Exit:
}

/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_ACUCOBOL

Description:  Error file handling for ACU COBOL.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
macro ERR_ACUCOBOL Trans2
{
	str	Fn[128];
	int Ln;
	if ( Search_Fwd('%?*, line [0-9]+:',0) )
	{
		Fn=Get_Word(',');
		for(;Ln=XPos('/',Fn,1);Fn=Str_Ins('\',Str_Del(Fn,Ln,1),Ln))
			;
		Forward_Till('0123456789');
		if ( Val(Ln,Get_Word(':')) )
			Return_Int = RC_ProblemParsing;
		else
		{
			Str Es;
			Right;
			Es=Remove_Space(Get_Word(''));
			rm('ERR_Find_Error /F='+FExpand(Fn)+'/L='+Str(Ln));
//			Return_Int=RC_DisplayError;
			Return_Str=Es;
		}
	}
	else
	{
		Return_Int = RC_NoMoreErrors;
		TOF;
	}
}

/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_Metaware

Description:  Error file handling for Metaware High C and Metaware Professional
							Pascal.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
macro ERR_Metaware  Trans2
{
	if ( Search_Fwd('%[EW] +"?*",',0) )
	{
		str Es,Fn[128];
		int Ln,Cn;
		if ( 'W'==Caps(Copy(Found_Str,1,1)) )
			Es='Warning: ';
		else
			Es='Error: ';
		Forward_Till('"');
		Right;
		Fn=Get_Word('"');
		for(;Ln=XPos('/',Fn,1);Fn=Str_Ins('\',Str_Del(Fn,Ln,1),Ln))
			;
		Forward_Till('0123456789');
		if ( Val(Ln,Get_Word('/')) )
			Return_Int=RC_ProblemParsing;
		else if ( Val(Cn,Copy(Get_Word(':'),3,8)) )
			Return_Int=RC_ProblemParsing;
		else
		{
			Right;
			Str T_Str=Get_Word('');
			Tabs_To_Spaces(T_Str);
			Es=Es+Remove_Space(T_Str);
			rm('ERR_Find_Error /F='+FExpand(Fn)+'/L='+Str(Ln)+'/C='+Str(Cn));
//			Return_Int=RC_DisplayError;
			Return_Str=Es;
		}
	}
	else
	{
		Return_Int=RC_NoMoreErrors;
		TOF;
	}
}

/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_ASPECT

Description:  Error file handling for PROCOMM's ASPECT script compiler.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
macro Err_Aspect Trans2
{
	int ln;
	str Es,Fn[128];

	if ( Search_Fwd('%{Error}||{Warning} ',0) )
	{
		Es=Get_Word(' ');
		if ( Search_Fwd('[0-9]+:',1) )
		{
			Val(ln,Get_Word(':'));
			Es=Es+Get_Word('');
			Mark_Pos;
			if ( Search_Bwd('%[~ ]+.ASP:',0) )
			{
				Fn=Get_Word(':');
				Goto_Mark;
				rm('ERR_Find_Error /F='+FExpand(Fn)+'/L='+Str(Ln));
//				Return_Int=RC_DisplayError;
				Return_Str=Es;
			}
			else
			{
				Return_Int=RC_ProblemParsing;
				Pop_Mark;
			}
		}
		else
			Return_Int=RC_ProblemParsing;
	}
	else
	{
		Return_Int=RC_NoMoreErrors;
		TOF;
	}
}

/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_SVS_FORTRAN

Description:  Error file handling for SVS FORTRAN compiler.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
macro Err_SVS_FORTRAN
{
	if ( Search_Fwd(' +[0-9]+. @*@*@*@*@*@*@*@*@*@*@*@*',0) )
	{
		str Em,Fn[128];
		int El,i;
		Down; Down; Forward_Till('*'); Forward_Till_Not('*'); Right;
		Em=Get_Word(' ')+': ';
		if ( Search_Fwd('in line',1) )
		{
			Forward_Till('0123456789');
			if ( !Val(El,Get_Word(' ')) )
			{
				if ( Search_Fwd('file',1) )
				{
					Forward_Till(' '); Right;
					Fn=Get_Word(' ');
					if ( SVL(Fn)==(i=XPos('.',Fn,1)) )
						Fn=Copy(Fn,1,i);
					Down; Down; First_Word; Forward_Till('*'); Forward_Till_Not('*'); Right;
					Em+=Get_Word('');
					rm('ERR_Find_Error /F='+Fn+'/L='+Str(El));
					Return_Str=Em;
//					Return_Int=RC_DisplayError;
				}
				else
					Return_Int=RC_ProblemParsing;
			}
			else
				Return_Int=RC_ProblemParsing;
		}
		else
			Return_Int=RC_ProblemParsing;
	}
	else
	{
		Return_Int=RC_NoMoreErrors;
		TOF;
	}
}


macro Err_MCC68000_C Trans Trans2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  Err_MCC68000_C

Description:  Error file handling for Microtec Research MCC68K C Compiler

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str Fn[80],Es;
	int Ln, Cn;

	if ( Search_Fwd('%{"?+"}', 0) ) {
		Fn = Copy(Found_Str, 2, Length(Found_Str) - 2);
		for(;Ln=XPos('/',Fn,1);Fn=Str_Ins('\',Str_Del(Fn,Ln,1),Ln))
				;
		Search_Fwd('line [0-9]+', 1);
		Forward_Till('0123456789');
		if ( VAL(Ln, Get_Word(' ;')) ) {
			Return_Int = RC_ProblemParsing;
			Goto Exit;
		}
		if ( ';'!=Cur_Char )
		{
			Forward_Till('0123456789');
			if ( VAL(Cn, Get_Word(';')) ) {
				Return_Int = RC_ProblemParsing;
				Goto Exit;
			}
		}
		Right; Right;
		Es = Get_Word('');
		rm('Err_Find_Error /F='+Fn+'/L='+str(Ln)+'/C='+str(Cn));
		if ( RC_DisplayError == Return_Int )
			Return_Str = Es;
		else
			Return_Str = Fn;
		}
	else
		Return_Int = RC_NoMoreErrors;
Exit:
}


macro Err_2500AD_C Trans Trans2
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  Err_2500AD_C

Description:  Error file handling for 2500AD C Compiler

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
{
	str Fn[80],Es;
	int Ln,ASM=0;

	if ( Search_Fwd('%?* : Line',0) )
	{
		// Check to see if it is the assembler
		if ( ' ' == Cur_Char )
		{
			ASM++;
			Forward_Till('*');
			Forward_Till_Not('*');
			Right;
		}
		Fn = Get_Word(' ');
		Forward_Till('0123456789');
		if ( !Val(Ln, Get_Word(' ')) )
		{
			if ( ASM )
			{Down;First_Word;}
			else
			{Right;Right;Right;}
			Es = Get_Word('');
			rm('Err_Find_Error /F='+Fn+'/L='+str(Ln));
			if ( RC_DisplayError == Return_Int )
				Return_Str = Es;
			else
				Return_Str = Fn;
		}
		else
			Return_Int = RC_ProblemParsing;
	}
	else
		Return_Int = RC_NoMoreErrors;
}

macro ERR_AMD_HIGHC29K Trans2
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  Err_AMD_HIGHC29K

Description:  Error file handling for Advanced Micro Devices HighC 29K C
							Compiler

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
{
	str Fn[128],Es;
	int Ln,Cn;

	if ( Search_Fwd('%? "?+"',0 ) )
	{
		Es = Caps(Cur_Char)+': ';
		Fn = Copy(Found_Str, 4, Length(Found_Str) - 4);
		Search_Fwd(',L[0-9]+',1);
		Right;Right;
		if ( Val(Ln, Get_Word('/c')) )
		{
			Return_Int = RC_ProblemParsing;
			Goto Exit;
		}
		Right;Right;
		if ( Val(Cn,Get_Word(': ')) )
		{
			Return_Int = RC_ProblemParsing;
			Goto Exit;
		}
		Right;
		Es += Remove_Space(Get_Word('')) + ' ';
		Mark_Pos;
		while ( 1 )
		{
			Down;
			Goto_Col(1);
			if ( Cur_Char != "|" )
				break;
			Right;
			Es += Remove_Space(Get_Word('')) + ' ';
		}
		Goto_Mark;
		rm('Err_Find_Error /F='+Fn+'/L='+Str(Ln)+'/C='+str(Cn));
		if ( (RC_DisplayError == Return_Int ) )
			Return_Str = Es;
		else
			Return_Str = Fn;
	}
	else
		Return_Int = RC_NoMoreErrors;
Exit:
}

macro ERR_TDBS TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_TDBS

Description:  Error file handling TDBS

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str fname[120] = Global_Str('last_compiled_window'),
			mstr;
	int ln, col ;

	Return_Int = False;
	if ( (Search_Fwd('{%@[ *[0-9]}||{%@*Missing Procedures@*}',0))  )
	{
		if ( Found_Str != '*Missing Procedures*' )
		{
			Forward_Till('0123456789');
			if ( Val(Ln,Get_Word(']')) )
			{
				Return_Int = RC_ProblemParsing;
				goto EXIT;
			}
			Forward_Till('0123456789');
			Mstr = Get_Word(']');
			Right;
			Mstr += ': ' + Get_Word('');
			Mark_Pos;
			Search_Bwd('%Compiling @[',0);
			Forward_Till('[');
			Right;
			Fname = Remove_Space(Get_Word(']'));
			Goto_Mark;
		}
		if ( Found_Str == '*Missing Procedures*' )
		{
			Ln = 0;
			MStr = 'Missing Procedures';
		}
		rm('ERR_Find_Error /F=' + fname +
				"/L="+Str(Ln));
		if (return_int == RC_CantFind) {
			Return_Str = FName;
			goto EXIT;
		}
		Return_Str = MStr;
	}
	else
		Return_Int = RC_NoMoreErrors;
EXIT:
}

macro ERR_SASM TRANS2 {
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  ERR_SASM

Description:  Error file handling SASM

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/

	int Ln;
	str Fn[128],Es;
	if ( Search_Fwd("%\x07*File -> ?*at line #[0-9]*",0) )
	{
		Ln = Parse_Int("at line #",Found_Str);
		Forward_Till(' '); Right; Forward_Till(' '); Right;
		Fn = Get_Word(' ');
		Forward_Till_Not(' ');
		Es = Get_Word('');
		rm('Err_Find_Error /F='+Fn+'/L='+Str(Ln));
		if ( (RC_DisplayError == Return_Int ) )
			Return_Str = Es;
		else
			Return_Str = Fn;
	}
	else
		Return_Int = RC_NoMoreErrors;
}

macro Err_Watcom_CPP TRANS2
{
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  Watcom_CPP

Description:  Error handling for Watcom_C_9.01.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str Fn[128],Es;
	int Ln,Cn;

	if ( (Search_Fwd('%([0-9]+,[0-9]+)',0)) )
	{
		right;
		if ( !Val(Ln,Remove_Space(Get_Word(',)'))) )
		{
			right;
			if ( !Val(Cn,Remove_Space(Get_Word(',)'))) )
			{
				right;right;right;
				Es = Remove_Space(Get_Word(''));
				Mark_Pos;
				Search_Bwd('%File:[ |9]+',0);
				Forward_Till(' |9|255');
				Right;
				Fn = Get_Word('');
				Goto_Mark;
				rm('ERR_Find_Error /F='+Fn+'/L='+Str(Ln)+'/C='+Str(Cn));
				if ( RC_CantFind == Return_Int )
					Return_Str = Fn;
				else
					Return_Str = Es;
			}
			else
				Return_Int = RC_ProblemParsing;
		}
		else
			Return_Int = RC_ProblemParsing;
	}
	else
		Return_Int = RC_NoMoreErrors;
}

macro Err_Meridian_Ada	TRANS2
{
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  Err_Meridian_Ada

Description:  Error handling for Meridian ADA V4.1.

							 (C) Copyright 1993 by American Cybernetics, Inc.
*******************************************************************************/

	Str Fn[128], Es;
	int Ln;


	if ( Search_Fwd('%"?+",',0) )
	{
 		right;
	  Fn = Get_word('"');
		Forward_Till('0123456789');
		if ( val(ln, get_word(':')) )
			{
				Return_Int = RC_ProblemParsing;
				goto EXIT;
			}
		Es=Remove_Space(Get_Word(''));
	}
	else
	{
		Return_Int=RC_NoMoreErrors;
		TOF;
		goto EXIT;
	}
	rm('ERR_Find_Error /F=' + Fn + '/L='+Str(Ln));
	if (return_int == RC_CantFind) {
		Return_Str = FN;
		goto EXIT;
	}
	Return_Str = Es;
EXIT:
}


macro Err_Softools	TRANS2
{
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  Err_Softools

Description:  Error handling for Softools C and ASM.

							 (C) Copyright 1993 by American Cybernetics, Inc.
*******************************************************************************/
	str fn[120] = Get_Path(Global_Str('last_compiled_window')),
			mstr;
	int ln;

	if ( Search_Fwd('%?+@[[0-9]+@] - {Warning}||{Error}||{Fatal}',0 ))
	{
		fn = fn + get_word('[');
		right;
		if ( Val(Ln,Get_Word(']')) )
		{
			Return_Int = RC_ProblemParsing;
			goto EXIT;
		}
		right;right;right;
		mstr = Get_word('');
	}
	else
	{
		Return_Int=RC_NoMoreErrors;
		TOF;
		goto EXIT;
	}
	rm('ERR_Find_Error /F=' + Fn + '/L='+Str(Ln));
	if (return_int == RC_CantFind)
	{
		Return_Str = FN;
		Goto Exit;
	}
	Return_Str = Mstr;

EXIT:
}

macro Err_TI_C	TRANS2
{
/*******************************************************************************
															MULTI-EDIT MACRO

Name:  Err_TI_C

Description:  Error handling for TI C Compiler.

							 (C) Copyright 1993 by American Cybernetics, Inc.
*******************************************************************************/
	str fn[120] = Get_Path(Global_Str('last_compiled_window')),
			mstr;
	int ln;

	if ( Search_Fwd('%"?+", line',0 ))
	{
		right;
		fn = fn + get_word('"');
		forward_till('0123456789');
		if ( Val(Ln,Get_Word(':')) )
		{
			Return_Int = RC_ProblemParsing;
			goto EXIT;
		}
		right;right;
		mstr = Get_word('');
	}
	else
	{
		Return_Int=RC_NoMoreErrors;
		TOF;
		goto EXIT;
	}
	rm('ERR_Find_Error /F=' + Fn + '/L='+Str(Ln));
	if (return_int == RC_CantFind)
	{
		Return_Str = FN;
		Goto Exit;
	}
	Return_Str = Mstr;

EXIT:
}

void AutoArrangErrSrc(str m_str) TRANS2 {
/*******************************************************************************
													 MULTI-EDIT MACRO FILE LANGUAGE

Name:  AutoArrangErrSrc

Description: Supports the Auto-Arrange compiler error window feature

               (C) Copyright 1993 by American Cybernetics, Inc.
*******************************************************************************/
	int jx, Cur_Win_Id = Window_Id, Err_Win_Id, Save_Refresh = Refresh;
  str T_CLine;

	Refresh = False;

	// *** Find the error window
	jx = 0;
	while(  (jx < window_count) & (Caps(Truncate_Path(File_Name)) != (user_id + 'MEERR.TMP')) )
	{
		jx = jx + 1;
		Switch_Window(cur_window + 1);
	}

	// *** If not found then exit
	if ( Caps(Truncate_Path(File_Name) ) != (user_id + 'MEERR.TMP') )
		goto Exit;

	if ( (0 == Global_Int('!AutoArrangErrSrc')) || (Window_Id == Cur_Win_Id) )
		Goto SkipAttach;

	if ( 0 == L_Color )
		L_Color=(((T_Color&0x7)<<4)|((T_Color&0x70)>>4));

	Err_Win_Id = Window_Id;                   //  Save error window ID

	// *** Unattach error window from the current window it is attached to
  jx = global_int("~MEERR_SPLIT_ID");
  set_global_int("~MEERR_SPLIT_ID",0);

//  RM('Window^DelWin /NODEL=1');
  DELWIN(1,0,0);

  set_global_int("~MEERR_SPLIT_ID",jx);

	if ( Global_Str('!WINZOOM#' + Str(Window_Id)) != '' )
		Set_Global_Str('!WINZOOM#' + str(window_id), ''); //  Set zoom status manually

	// *** Switch back to current source window and attach the error window to it
	Switch_Win_id(Cur_Win_Id);
	if ( Window_Attr & 0x40 )
		rm('Window^Zoom');
	Window_Attr=Window_Attr&0xfe;
	Set_Global_Int('~MEERR_SPLIT_ID', Cur_Win_Id );

// check for out of range values of ~MEERR_HEIGHT
  if (Global_int("~MEERR_HEIGHT") < 2) {
    Set_Global_Int("~MEERR_HEIGHT", 2);
  }
  if (Global_int("~MEERR_HEIGHT") > (max_window_row - min_window_row - 4)) {
    Set_Global_Int("~MEERR_HEIGHT", max_window_row - min_window_row - 4);
  }

  RM('Window^SplitWin /DIR=Down/LN#=' + str(Global_int("~MEERR_HEIGHT") + 1) +
      '/ID=' + Str(Err_Win_Id));

	Refresh = True;
	Switch_Win_Id(Cur_Win_Id);
	new_screen;
	Refresh = False;
SkipAttach:
Exit:
	Switch_Win_Id(Cur_Win_Id);
	Refresh = Save_Refresh;
  if ('' != M_Str)
		Make_Message(M_Str);

} //  AutoArrangErrSrc

macro TEMPLATE TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:  TEMPLATE

Description:  Checks for the existence of the language specific global
							variable that contains the template data for that language
							if it exists then it will be used for the template expansion
							otherwise calls the appropriate template macro for the filename
							extension of the current file.  The global variable name is
							determined by a '!' followed by the first 3 characters of the
							language type followed by '.TMPLT0'.  For example the pascal
							global would be '!PAS.TMPLT0' the C global would be '!C.TMPLT0'
							The macro name is determined by the first three characters
							of the language type followed by  '_IND'.  For example, the
							pascal macro would be 'PAS_IND', the C macro would be 'C_IND'

		'îC=' = Expansion case type.
								0 = case sensitive.    Keyword  - case sensitive
																			Expansion - verbatim.
								1 = case insensitive.  Keyword  - case insensitive
																			Expansion - All caps.
								2 = case insensitive.  Keyword  - case insensitive
																			Expansion - First letter caps.
								3 = case insensitive.  Keyword  - case insensitive
																			Expansion - dependent on keyword.
		'îM=' = Minimum number of characters in keyword required for an
							expansion to occurr.
	238 - 'î' = Parameter delimiter
	127 - '' = Field separator
	 20 - '' = Carriage return (Run CR macro)
	174 - '®' = Carriage return (Goto starting column)
	196 - 'Ä' = Record cursor position
	 17 - '' = Move cursor left
	 16 - '' = Move cursor right
	 24 - '' = Move cursor up
	 25 - '' = Move cursor down
	 64 - '@' = Translate next character literally
	168 - '¨' = Remember current column position
	173 - '­' = Goto remembered column number
	240 - 'ð' = Goto starting column
	241 - 'ñ' = Toggle Insert mode
	251 - 'û' = Run macro:  "/*ûC^CCOMMENT"
	252 - 'ü' = Expand template for preceding character (be carefull about
								infinite loops)
	237 - 'í' = Do nothing (used to stop 'keyword' interpretation)
	242 - 'ò' = Indent and set indent level
	243 - 'ó' = Undent and set indent level
	128 - '€' = Expand only in comment.

	Parameters:  /S= - If non-zero then the key pressed will be typed out if
										 There is no template to expand.

							/O= - Override minimum expansion.


							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
#define FIELDSEP    127
#define CARRIAGE1    20
#define CARRIAGE2   174
#define CURSOR      196
#define MVLEFT       17
#define MVRIGHT      16
#define MVUP         24
#define MVDOWN       25
#define XLATELIT     64
#define REMCOL      168
#define GOCOL       173
#define GOSCOL      240
#define TOGINS      241
#define MACRUN      251
#define TEMPL       252
#define DONOTHING   237
#define _INDENT     242
#define _UNDENT     243
#define COMMENTONLY 128

	char  CurChar;
	str   KeyWord[20], ExpandStr[100], TStr[16],
				TemplateName[16],PrePend='';
	int   old_refresh=refresh,StrPos, StrLen, OrigCol, CCol = 0, CLine, RCol = 0, SKCol,
				OCol = C_Col, TemplateStat = 0, CaseSense, MinExpand, jx,
				tins = insert_mode,
				space_expand = parse_int("/S=", mparm_str ),
				FirstChar = 1;

	push_undo;
	insert_mode = TRUE;
	if( (keywords_str != "") && space_expand ) {
		if( line_stat & 0x30 ) {
			PrePend='€';
		}
	}
	Mark_Pos;
	if ( Global_Str('.' + Get_Extension(File_Name)) != '' )
	{
		TStr = Parse_Str('LS=',Global_Str('.' + Get_Extension(File_Name)));
		if ( Tstr != '' )
		{
			if ( "" != Global_Str(TemplateName = "!"+Copy(TStr,1,3)+".Tmplt0") )
			{
				Refresh = False;
				if ( space_expand )
				{
					First_Word;
					SKCol = C_Col;
					if ( PrePend == (KeyWord = PrePend+Shorten_Str(Get_Word(''))) )
						KeyWord = "";
				}
				else
				{
					while ( (C_Col > 1) )
					{
						Left;
						if ( Pos(Cur_Char,' |9|255') )
						{
							Right;
							break;
						}
					}
					SKCol = C_Col;
					KeyWord = ""+Get_Word(' |9|255');
				}
				Mark_Pos;
				First_Word;
				OrigCol = C_Col;
				Goto_Mark;

				jx = SVL(KeyWord);

				if ( 0 == (MinExpand = Parse_Int('/O=', MParm_Str)) )
					MinExpand = Parse_Int("îM=", Global_Str(TemplateName));

				if ( (jx < (1+MinExpand)) || !MinExpand )
					ExpandStr = "";
				else
				{
					switch ( CaseSense = Parse_Int("îC=", Global_Str(TemplateName)) )
					{
						case 0 :
							ExpandStr = Parse_Str(KeyWord, Global_Str(TemplateName));
							break;

						case 1 :
						case 2 :
						case 3 :
							CurChar = Str_Char(KeyWord, 2);
							KeyWord = Lower(KeyWord);
							if ( "" != (ExpandStr = Parse_Str(KeyWord, Global_Str(TemplateName))) )
							{
								Goto_Col(SKCol);
								Del_Chars(jx - 1);
								ExpandStr = Copy(KeyWord, 2, jx-1)+ExpandStr;
							}
							if ( (CaseSense == 3) && (CurChar == Caps(CurChar)) )
								ExpandStr = Caps(ExpandStr);
							break;
					}
				}
				StrLen = SVL(ExpandStr);

				for ( StrPos = 1; StrPos <= StrLen; StrPos++)
				{
					TemplateStat++;
					switch ( ASCII(CurChar = Str_Char(ExpandStr, StrPos)) )
					{
						case CARRIAGE1 :
							rm('CR');
						case DONOTHING :
							break;

						case CARRIAGE2 :
							mark_pos;
							goto_col( OrigCol );
							set_indent_level;
							goto_mark;
							Cr;
							break;

						case CURSOR :
							CCol = C_Col;
							CLine = C_Line;
							break;

						case MVLEFT :
							Left;
							break;

						case MVRIGHT :
							Right;
							break;

						case MVUP :
							up;
							break;

						case MVDOWN :
							down;
							break;

						case XLATELIT :
							StrPos++;
							Text(Str_Char(ExpandStr, StrPos));
							break;

						case REMCOL :
							RCol = C_Col;
							break;

						case GOCOL :
							if ( RCol )
								Goto_Col(RCol);
							break;

						case GOSCOL :
							Goto_Col(OrigCol);
							break;

						case TOGINS :
							if ( Insert_Mode )
								Insert_Mode = False;
							else
								Insert_Mode = True;
							break;

						case MACRUN :
							rm(Remove_Space(Copy(ExpandStr, 1+StrPos, StrLen - StrPos)));
							StrPos = StrLen;
							break;

						case TEMPL :
							rm('TEMPLATE /O=1');
							break;

						case _INDENT :
							Indent;
							break;

						case _UNDENT :
							Undent;
							break;

						default :
							switch ( CaseSense )
								{
									case 1 :
										CurChar = Caps(CurChar);
										break;

									case 2 :
										if ( FirstChar )
										{
											CurChar = Caps(CurChar);
											FirstChar = 0;
										}
										break;
								}
							Text(CurChar);
					}
					if ( (Caps(CurChar) == Lower(CurChar)) && (!Pos(CurChar, '')) )
						FirstChar = 1;
				}
				if ( CCol )
				{
					Goto_Col(CCol);
					while( (c_line > cline) && (c_line > 1) )
						up;
					while( (c_line < cline) && !at_eof )
						down;
				}
				else if ( 0 == TemplateStat)
					Goto_Col(OCol);
				if ( strlen ) {
					Refresh = old_refresh;
					Redraw;
				}
			}
			else if ( !Parse_Int("/S=", MParm_Str) )
			{
				RM(Tstr+'^'+Copy(TStr,1,3)+'TEMP');
				if ( Error_Level != 0 )
				{
					Make_Message('NOT Supported for this file extension.');
					Error_Level = 0;
				}
			}
		}
	}
	if ( (0 == TemplateStat) && space_expand )
	{
 done:
		Goto_Mark;
 done2:
		refresh = old_refresh;
		insert_mode = tins;
		// if no template expanded then output the key that was pressed.  If same key
		//  pressed then continue outputting key until different key is pressed.
		Text(Char(Key1));
		if ( !Global_Int("~In_Repeat") )
		{
			int k1=Key1;
			Push_Undo;
			Read_Key;while(k1==Key1){Text(Char(Key1));Read_Key;}
			Pop_Undo;
			Pass_Key(Key1,Key2);
		}
	}
	else
		Pop_Mark;
	pop_undo;
}

macro MATCH TRANS2 {
/*******************************************************************************
																MULTI_EDIT MACRO

Name:  MATCH

Description:  Calls the appropriate matching macro for the filename extension
	of the current file.  The macro name is determined by the first three
	characters of the language type followed by 'MTCH'.  For example, the
	pascal matching macro would be named 'PASMTCH', the C macro would be 'CMTCH'

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	str TStr;
	int jx;
	if ( Global_Str('.' + Get_Extension(File_Name)) != '' ) {
		TStr = Parse_Str('LS=',Global_Str('.' + Get_Extension(File_Name)));
		if ( Tstr != '' ) {
			RM( TStr + '^' + Copy(TStr,1,3)+'MTCH /HI=1' );
			if ( Error_Level != 0 ) {
				GOTO No_Support;
				 }
			GOTO Exit;
			}
	 }
NO_Support:
	Make_Message('NOT Supported for this file extension.');
	Error_Level = 0;
EXIT:
}
