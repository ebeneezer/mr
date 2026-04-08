
macro_file sessmgr;
/********************************************************************************
															 MULTI-EDIT MACRO FILE

Name: SESSMGR

Description:	Provides support for the ME Session Manager

Routines:

sm_make_list	 Creates a new window and builds of a list of the directories
 							 currently covered by encoded status files.
							 Leaves the cursor on the line for the current directory.

m_dialog       the dialog box from which the user selects sessions
							 the user can insert new sessions, delete sessions from
							 the list, select a session from the list, or escape.
SessMgr   		 restarts Multi-Edit
SM_Not_Active	 Inform the user that the Session Manager can not be used
							 unless the Restore feature is configured for 'Encoded Files'
SessMgr				 restart the me session with new restore status files

								(C) Copyright 1993 by American Cybernetics, Inc.
****************************************************************************** */



// we need the prototypes for calls to macros within exit.s
#include exit.sh

// to remember what  restore=3  is
#define ENCODED_FILES_MODE	(global_int('RESTORE')==3)

/******************** Multi-Edit VOID Macro Function ************************

 NAME:         cantdelete()

 DESCRIPTION:  notify the user that they cant delete the session that
							 they are currently working in from the session list

 PARAMETERS:	 None.

 RETURNS:			 None.

*****************************04-05-93 04:20pm*******************************/

void cantdelete()
{
	int  menu = menu_create ;
	menu_set_item( menu, 1, 'From the SESSION MANAGER: ','','/L=3/C=3',10,0, 0);
	menu_set_item( menu, 2, 'You may not delete the current','','/L=5/C=3',10,0, 0);
	menu_set_item( menu, 3, 'session that you are working on.','','/L=6/C=3',10,0,0);
	menu_set_item( menu, 4, 'Select another session.','','/L=8/C=3',10,0,0);

	return_int = menu;
	RM('UserIn^Data_In /HN=1/H=*/A=1/#=4/T= WARNING ');
	menu_delete( menu );
}

/****************** Multi-Edit INTEGER Macro Function ***********************

 NAME:         sm_make_list(session_list_id)

 DESCRIPTION:  Creates a new window and builds of a list of the directories
 							 currently covered by encoded status files.
							 Leaves the cursor on the line for the current directory.


 PARAMETERS:   session_list_id, the window on which to create the file list

 RETURNS:      the length of the longest line in the file list

 ----------------------------------------------------------------------------
 MODIFICATIONS
 ----------------------------------------------------------------------------
 040893[scm]: while creating the list of sessions, the authenticity of the
							status files is kept by checking the existence of the
 							directories that the encoded status files refer to.
							If the directory does not exist, the status file is deleted.

 091093[scm]:	actually delete the status file / thx2 jeff fontanesi

*****************************04-05-93 04:06pm*******************************/
int sm_make_list(session_list_id)
			 trans
{
  str fn[128] = make_restr_name( 3 );	// restore file for current dir
  str buf;
	str bff,bff2;
  int tfile_search_attr = file_search_attr;
  int sresult,hn,jx,jy, fexists;
	int SV_Refresh = refresh;


  switch_win_id( session_list_id ); // shouldn't we already be there?
	erase_window; 										// clear the window
	refresh = false;

	// 082493[scm]
	// display 'search in progress
	Put_Box(13, 8, 50, 11, LIGHTGRAY, BLACK, '', TRUE);
	Write('Searching for All Sessions...',16,9,LIGHTGRAY,BLACK);

  file_search_attr = 1;

  // Find first file in list
  sresult = first_file( get_path(fn) + "*.ME" );
    // As long as dos is returing 0, loop
  while( !sresult )
  {
      // Open the file up
    if( !s_open_file( get_path(fn) + last_file_name, 0x20, hn ) )
    {
        // read the first 180 bytes
      if( !s_read_bytes( buf, hn, 150 ))
      {
          // check to see if it is a valid restore file
        if(copy(buf, 1, 22) == ("@MULTI-EDIT VERSION "  + Copy(version,1,2)))
        {
            // is so, then find the start of the path name
          jx = 22;
          while( str_char( buf, ++jx ) != " " );
          while( str_char( buf, jx ) == " ")
          {
            ++jx;
          }
            // find the end of the line
          jy = xpos( "\r", buf, jx );
          if ( jy != 0 )
          {
              // store the path in the list file
						bff = copy(buf, jx, jy - jx );

						call dir_exists;
						if ( !fexists )
						{
							// the file is already open...
							s_close_file(hn);													// thx jeff
							del_file(get_path(fn) + last_file_name);
						}
						else
						{
	            put_line( bff );

	              // put the filename beyond view
	            goto_col( 150 );
	            text( last_file_name );
	            down;
						}
          }
        }
      }
      s_close_file(hn);
    }
    sresult = next_file;
  }

	// 0842493[scm]
	// kill the 'searching...' box
	Kill_Box;

	up;         // Sort the list
	qsort_lines( 1, c_line,1, 1, 2048, 0 );

  tof;

  search_fwd(truncate_path(fn) + ".ME", 0 ); // find current dir in list
  jx = c_line;
  tof;
  while( c_line < jx )		// adjust line pointer to that of current dir
    down;

  file_search_attr = tfile_search_attr;

	// 3-23-93(scm) ----------------------------------------------------

	// find the longest line in the file list
	int count_of_lines, longest_line_length, temp_length;
	longest_line_length = 0;
	eof;
	count_of_lines = c_line+1;
	tof;
	while ( c_line < count_of_lines )
	{
		buf = get_line;
		jx = xpos(' ',buf,1);
		if ( jx > longest_line_length )
		{
			longest_line_length = jx;
		}
		down;
	}
	tof;

	refresh = SV_Refresh;

	// return the length of the longest filespec line for dispaly purposes
	return (longest_line_length);


// 082393[scm] use system function file_exists()
dir_exists:
	int p,c;

	p= xpos(":", bff, 1);
	if(p > 0)
	{
		if((p == (svl(bff) -1 )) && (str_char(bff,p + 1) == "\\"))
		{
			fexists = 1;
			ret;
		}
	}

	bff2 = bff;
	p = svl(bff2);
	c = str_char(bff2,p) == '\';
	if ( (c) && (p > 1) )
	{
		if ( str_char(bff2,p-1) == ':' )
		{
			bff2 = copy(bff2,1,p);
		}
		else
		{
			bff2 = copy(bff2,1,p-1);
		}
	}
	fexists = file_exists(bff2);
	if ( fexists )                     // File_Exists() returns 2 if the
	{                                  // passed name is a directory
			fexists--;                     // so...
	}                                  //   if subdir, return 1
	ret;                               //   otherwise, return 0

}


/******************** Multi-Edit VOID Macro Function ************************

 NAME:         sm_delete()

 DESCRIPTION:  Run by SM_DIALOG when the Del button is pressed

*****************************04-18-93 01:09pm*******************************/
void sm_delete()
{
	// 082493[scm] add in check for deleting current session

	str tstr1;

	goto_col(1);
	tstr1 = get_word(' |9|255');
	if ( tstr1 == fexpand('') )
	{
			cantdelete();
	}
	else
	{
		RM("USERIN^VERIFY /H=PROMPTS/BL=CONFIRM/T=Delete \""+ tstr1 + "\"?");
		if (Return_Int)
		{
				// delete the encoded file
				forward_till_not(' |9|255');
				tstr1 = get_path(make_restr_name( 3 )) + '\' + get_word("");
				del_file(tstr1);

				// Delete the line from the session list
				del_line;
		}
	}
	goto_col(1);
	return_int = 0;
}



/****************** Multi-Edit INTEGER Macro Function ***********************

 NAME:         sm_dialog

 DESCRIPTION:  the dialog box from which the user selects sessions
							 the user can insert new sessions, delete sessions from
							 the list, select a session from the list, or escape.

 PARAMETERS:   int longest_line_length
									Used to evenly display the session list
							 int session_list_id,
									The window where the list has been built
							 int session_list_number,
									The number of the window where the list has been built
									Used by the dialog box /WIN parameter (where's the list?)


 RETURNS:      Directory name of session selected, or "" if ESC was hit.

*****************************04-05-93 04:22pm*******************************/
str sm_dialog(	int longest_line_length,
										int session_list_id,
										int session_list_number )
				trans
{
	int Active_Window = 	Window_Id;
	int T_Insert_Mode = 	Insert_Mode;
	int T_Refresh = 			Refresh;
	int cc = 							longest_line_length;		// cc vars used for box
	int ll;
	int menu = menu_create;
	str buffer1;

	Tof;

MENU_LOOP:

	refresh = false;

	if(longest_line_length > (screen_width - 5))
		longest_line_length = screen_width - 5;
	if( longest_line_length < 42 )
	  longest_line_length = 42;
	cc = (longest_line_length / 2) - 20 + 3;

	// window should be at least 40 characters wide
	menu_set_item(menu,1,'New directory', '', '/KC=<Ins>/W=18/K1=0/K2=82/R=3/L=2/C=' + str(cc), 11, 0, 0);
	menu_set_item(menu,2,'Remove session', 'SM_DELETE', '/M=1/KC=<Del>/W=19/K1=0/K2=83/R=4/L=2/C=' + str(cc + 20), 11, 0, 0);
	menu_set_item(menu,3,'Select directory to work in:', '', '/ML=0/W=' + str(longest_line_length + 1) + '/L=4/C=2/DC=1/WIN='+STR(session_list_number)+'/OR='+STR(C_ROW), 15, 0, 0);

	Return_Int = menu;
  RM("USERIN^DATA_IN  /H=SESSMGR/HN=1/A=0/PRE=SS/#=3/T=Session Manager/S=3/NC=1/OR=" + Str(C_Row));
forced:
	if (Return_Int == 1) {
		goto_col(1);
		buffer1 = get_word(' |9|255');  // Return the session string
	}
	else if ( Return_Int == 0 )
	{
		Return_Int = 0;						// user aborted
		buffer1 = "";
	}
	else if ( Return_Int == 3 )		// *** NEW SESSION ***
	{
		Return_Str = "";
    rm("QueryBox /W=50/ML=128/H=SESSMGR/T=Enter directory name");

		if ((Return_Int != 0) && (Return_Str != ""))	// *** ENTER ***
		{
			buffer1  = fexpand(return_str);
			if((svl(buffer1) > 3) && (copy(buffer1,svl(buffer1),1) == "\\"))
			{
				buffer1 = copy(buffer1,1,svl(buffer1) - 1 );
			}
			if((file_attr(buffer1) & 0x10) == 0)
			{
				rm("MEERROR^MessageBox /B=1/M=Directory does not exist");
				goto menu_loop;
			}
		}
		else if ( Return_Int == 0 ) // *** ESCAPE ***
		{
			goto MENU_LOOP;
		}
	}
	else
		Goto MENU_LOOP;

	Insert_Mode = T_Insert_Mode;
	Refresh = T_Refresh;
	menu_delete(menu);
	return ( buffer1 );
}


//======================================================================
//	New routines to simulate mesys^parms1 and mesys^parmload code that
//	sets temp global variables (those whose prefix is one of !@.~)
//======================================================================
void SM_PARMS1()
{
	int Parm_Number;
	int jx, default_back, kspeed ;
	str TStr;
	char Tchar;

	set_global_int("@NO_MOUSE_SET", TRUE );
	return_str = temp_path;

	Set_Global_Str('~TEMP_PATH', temp_path );
	rm('XlateCmdLine');
	temp_path = return_str;
	return_str = backup_path;

	Set_Global_Str('~BACKUP_PATH', backup_path );
	rm('XlateCmdLine');
	backup_path = return_str;

	kspeed = (Global_Int('NO_KEYSPEED') == 0);

		 /* Do we need to set default colors? */
	if(  global_int('DEFAULT_COLORS')  ) {
		intr($11);
		if(  (r_ax & $0030) == $0030  ) {
			call set_mono;
		} else {
			call set_color;
		}
	}

	if(  kspeed  ) {
		R_BX = (Global_Int('KEYDELAY') << 8) | GLOBAL_INT('KEYSPEED');
		R_AX = $0305;
		INTR( $16 );
		Set_Global_Int('NO_KEYSPEED', 0 );
	}
	goto exit;

color_defaults:
	stat1_color = lightgray | default_back;
	stat2_color = white | default_back;
	message_color = lightgray | default_back;
	fnum_color = lightgray | default_back;
	fkey_color = 112;
	w_t_color = LightGray | default_back;
	w_s_color = yellow | default_back;
	w_b_color = lightgray | default_back;
	w_h_color = 112;
	w_eof_color = white | default_back;
	w_l_color = yellow | default_back;
	w_lb_color = 113;
	w_c_color = white | default_back;
	m_t_color = LightGray | default_back;
	m_s_color = white | default_back;
	m_b_color = white | default_back;
	m_k_color = cyan | default_back;
	m_h_color = 112;
	d_t_color = m_t_color;
	d_s_color = m_s_color;
	d_b_color = m_b_color;
	d_h_color = m_h_color;
	h_t_color = lightgray | default_back;
	h_s_color = white | default_back;
	h_r_color = yellow | default_back;
	h_b_color = lightgray | default_back;
	h_h_color = 112;
	h_f_color = 112;
	h_t1_color = 33;
	h_t2_color = 41;
	h_t3_color = 240;
	working_color = 240;
	background_color = default_back | white;
	Error_color = white | default_back;
	Shadow_Color = LightGray | default_back;
	Shadow_Char = Char(177);
	button_color = 7;
	button_key_color = 15;
	button_shadow_color = default_back | lightgray;
  Set_Global_Str('@SYNTAX_COLORS','/RWC=271/SYC=271/ECC=880/SCC=265/C1C=880/C2C=880/NCC=0');
	ret;

set_color:
	Error_Color = 79;
	Shadow_Color = 8;
	Shadow_Char = '|0';
	W_T_Color = 23;
	W_H_Color = 96;
	W_B_Color = 27;
	W_C_Color = 31;
	w_l_color = 30;
	w_lb_color = 110;
	W_EOF_Color = 20;
	W_S_Color = 30;
	M_T_Color = 112;
	M_S_Color = 113;
	M_B_Color = 112;
	M_H_Color = 95;
	m_k_color = 120;
	CB_H_Color = 63;
	CB_T_Color = 48;
	CB_S_Color = 49;
	Button_Color = 27;
	Button_Key_Color = 30;
	Button_Shadow_Color = 120;
	D_T_Color = 112;
	D_S_Color = 113;
	D_B_Color = 112;
	D_H_Color = 80;
	H_T_Color = 23;
	H_T1_Color = 27;
	H_T2_Color = 28;
	H_T3_Color = 127;
	H_S_Color = 31;
	H_B_Color = 23;
	H_H_Color = 112;
	H_R_Color = 30;
	H_F_Color = 112;
	FKey_Color = 112;
	FNum_Color = 49;
	Stat1_Color = 112;
	Stat2_Color = 113;
	Message_Color = 112;
	Working_Color = 192;
	Background_Color = 23;
  Set_Global_Str('@SYNTAX_COLORS','/RWC=283/SYC=267/ECC=316/SCC=298/C1C=268/C2C=268/NCC=266');
	ret;

set_mono:
	CB_H_Color = 112;
	CB_T_Color = 7;
	CB_S_Color = 15;
	default_back = 0;
	Call Color_Defaults;
	Set_Global_Str('@SYNTAX_COLORS','/RWC=271/SYC=271/ECC=880/SCC=265/C1C=880/C2C=880');
	ret;

exit:
}

/******************** Multi-Edit VOID Macro Function ************************

 NAME:         clr_tmp_globals()

 DESCRIPTION:  Clears out global variables whose prefix is one of !@.~

 PARAMETERS:   None

 RETURNS:      None

*****************************09-15-93 03:45pm*******************************/
void clr_tmp_globals()
	// 091593[scm]
	// clear out globals that begin with one of [!@.~]
	// Originally was put into Exit^Status, but that proved dangerous
	// (autosave clobbered all settings stored in "!@~." variables)
	//
{
	int jx;
	str tstr;

	TStr =  First_Global( jx );
	while ( Tstr != '' )
	{
		if(  (XPOS(str_char(tstr,1),'!@.~',1) != 0)  ) {
			if(  jx == 1  ) {
				set_global_int(tstr,0);
			} else {
				set_global_str(tstr,'');
			}
			TStr = First_Global(jx);
		}
		else
		{
			TStr = Next_Global(jx);
		}
	}
}



/****************** Multi-Edit INTEGER Macro Function ***********************

 NAME:         sm_restart(

 DESCRIPTION:  restarts Multi-Edit

 PARAMETERS:   None.

 RETURNS:      False if failure, true if success

*****************************04-05-93 04:32pm*******************************/
int sm_restart( )
{
  int count, winid;
	int T_Refresh = refresh;

	// for use in clear_temp_globals subroutine:
	int jx;
	str tstr;


  rm("EXIT /NE=1");	// save current system status and shut down
  if ( return_int )
  {
		// this was prompted by noticing that the same files were always
		// checked out of my VCS library, yet vcs support was configured
		// for encoded status files.  found out some global variables were
		// not being cleared.
		clr_tmp_globals(); // added 091593[scm] - clear !@.~ prefixed vars.

		// kill all editable windows
		refresh=false;
		for ( count=WINDOW_COUNT; count > 0; count-- )
		{
		  winid = count;
			switch_window(winid);
			if ( (WINDOW_ATTR & $80) == 0 ) /* editable window? */
			{
				Redraw;
				Delete_Window;
			}
		}
 		make_message('');
		refresh=T_Refresh;
		return(1);                   // success
  }
  else
	{
		refresh=T_Refresh;
    return(0);									 // failed
	}
}


/******************** Multi-Edit VOID Macro Function ************************

 NAME:         SM_Not_Active()

 DESCRIPTION:  Inform the user that the Session Manager can not be used
							 unless the Restore feature is configured for 'Encoded Files'

 PARAMETERS:   None.

 RETURNS:      None.

*****************************04-05-93 04:34pm*******************************/
void SM_Not_Active()
{
	int  menu = menu_create ;
	menu_set_item( menu, 1, 'The Multi-Edit SESSION MANAGER','','/L=3/C=3',10,0, 0);
	menu_set_item( menu, 2, 'is not active at this time.','','/L=4/C=3',10,0, 0);
	menu_set_item( menu, 3, 'To activate the SESSION MANAGER, you must','','/L=6/C=3',10,0,0);
	menu_set_item( menu, 4, 'select "Encoded status files for each dir" ','','/L=7/C=3',10,0,0);
	menu_set_item( menu, 5, 'from the Restore Previous Status option','','/L=8/C=3',10,0,0);
	menu_set_item( menu, 6, 'from the User Interface Settings dialog box','','/L=9/C=3',10,0,0);
	menu_set_item( menu, 7, '                                             ','','/L=10/C=3',10,0,0);
	menu_set_item( menu, 8, 'Main Menu --','','/L=12/C=3',10,0,0);
	menu_set_item( menu, 9, 'Other --','','/L=13/C=6',10,0,0);
	menu_set_item( menu,10, 'Install --','','/L=14/C=9',10,0,0);
	menu_set_item( menu,11, 'User Interface --','','/L=15/C=12',10,0,0);
	menu_set_item( menu,12, 'Restore Previous Status','','/L=16/C=15',10,0,0);

	return_int = menu;
	RM('UserIn^Data_In /HN=1/H=INRE/A=1/#=11/T=Session Manager');
	menu_delete( menu );

}



/******************** Multi-Edit VOID Macro Function ************************

 NAME:         SessMgr()

 DESCRIPTION:  The driver routine of the Session Manager

 PARAMETERS:   None.

 RETURNS:      None.

*****************************04-05-93 04:36pm*******************************/
void SessMgr()
{
	int i;												// loop counter
	int session_list_number; 			// window number of filespec list
	int session_list_id;					// window id of filespec list
	int longest_line_length; 	 		// longest length in filespec list
	str buffer;										// will contain the filespec text

	// save the handle of the starting window for use if user presses escape
	int last_window_active = window_id;
	int window_to_switch_to;

	int T_Refresh = refresh;

	refresh = false;

	working;
	if ( !ENCODED_FILES_MODE ) // must be in Encoded Files mode!!!
	{
		SM_Not_Active();
	}
	else
	{


		// ask user for session to switch to
		int user_is_picking;
		user_is_picking = 1;
		while(user_is_picking)
		{
			create_window;  										// create window for session list
			session_list_number = cur_window;		// save its id
			session_list_id     = window_id;		//   and number

			// create the list and fetch the longest line length
			longest_line_length = sm_make_list(session_list_id);


			buffer = sm_dialog(				longest_line_length,
																session_list_id,
																session_list_number);
			if ( buffer != "" )
			{
				if ( (svl(buffer) > 3) && (str_char(buffer,svl(buffer)) == '\') )
				{
					buffer = copy(buffer,1,svl(buffer)-1);
				}
				working;
				switch_win_id(session_list_id); // we should already be there

					delete_window;
					redraw;
					working;

					// 04-06-93[scm]: should we not switch back to the saved active window?
					// by not doing so, rm(exit) begins having some unknown window active
					// which caused the restore to return to a window other than what
					// was active at the time of saving that sessions status.

					switch_win_id(last_window_active);
					working;
					// try to restart the system
					if ( sm_restart() )
					{
						// change to directory of new session
						change_dir(buffer);
						if ( Error_Level  )
						{
							Beep;
							make_message('unable to change directory to '+buffer);
							read_key;
							Error_Level = 0;
						}
						else
						{

		// ***** Clear up the display while switching *****
							refresh = true;
							redraw;							// 090993[scm]
							new_screen;
		// ************************************************

							working;
							rm( user_id + 'INIT' );
							working;
							// kill the file history we used ????
							// make next file->open use clean history list?
							str fhnum[4] = parse_str('/#=',global_str('FILE_HISTORY'));
							set_global_str('FILE_HISTORY'+fhnum,'');

							sm_parms1();
							RM('.STARTUP^STARTUP');
							rm('KEYMAC_LOAD /NE=1');
							ERROR_LEVEL = 0;
							rm("RESTORE /M=0"); // restore status according to that directory
							window_to_switch_to = return_int;
							user_is_picking = 0;

							// 090293[scm] - call macro named by @startup_mac_2
							// variables set by startup.mac be may overridden by
							// those contained in the status file just restored
							// (see mesys^parmload for the sequence of startup events)
							if(  (Length(Global_Str('@STARTUP_MAC_2')))  ) {
								RM(Global_Str('@STARTUP_MAC_2'));
								Error_Level = 0;
							}
						}
					}
					else
					{
							Make_Message('Session Manager: Save Files aborted. Returned to current session.');
					}

			}
			else
			{
				make_message("");
				delete_window; // yes, we should be at session_list_id window
				switch_win_id(last_window_active); // back to where we started
				user_is_picking = 0;
				switch_win_id(window_to_switch_to); // 082493[scm]
			}
		} /* end while user_is_picking */
	}
	refresh = T_Refresh;

}

