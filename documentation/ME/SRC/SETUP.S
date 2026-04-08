/*******************************************************************************
												MULTI-EDIT MACRO FILE SETUP
		Name: SETUP

Description:  The installation and setup utilities

SETUP           - The main Installation and Setup menu.
INTERFACESETUP  - A dialog box to configure user interface preferences.
CHECK_SETUP     - Checks for changed configuration and prompts for saving.
SETSAVE         - Generates and compiles INIT.S to save settings.
SETEMS          - Menu for configuring shell to DOS Ems and/or disk swapping.
SETSERIAL       - Input the Multi-Edit Serial Number.
SETKEYS         - The key remapping menu.
KEYLIST_CHECK   - Checks the keymap list.  To be used ONLY by SETKEYS!
KEY_CHECK       - Checks for key assigment conflicts.  ONLY for SETKEYS!
DEFINE_KEY      - Prompts for key to define in SETKEYS.
SETCOLOR        - The color installation menu.
COLORCHART      - Popup color chart for selecting screen attributes.
CUSTOMCOLOR     - Creates an interface to configure extension specific colors.
									To be run by EXTENS only!
DISPLAY_SETUP   - The display configuration menu.
SRCHSET         - The search configuration menu.
SETEDIT         - The edit settings menu.
SETMEPATHS      - Sets up the backup methods, backup and temp file paths.
SETMOUSE        - The mouse settings menu.
PRTTYPE         - The printer setup menu.
CHECK_PRT       - Special processing for PRTTYPE only.
PRINTER_DRIVER  - Menu of printer codes for the selected printer type.
EXTENS          - The filename extension specific setup menu.
CHECK_EXT       - Extension list processing.  To be used ONLY by EXTENS!
LANG_MENU       - Menu of language types.
PGM_MENU        - Menu of program types.
PGM_DB          - A menu of compiler interfaces for the selected extension.
PGM_WARNING     - Warns user of adding program types without support.
LANG_WARNING    - Warns user of adding language types without support.
EXT_FORMAT      - Creates and/or edits extension specific format lines.
TGLVID          - Toggles video mode.
KEYCODE         - Get key1 and key2 scan codes for any keystroke.
MAKEKEY         - Get key name from 2 scan codes.
MAKE_SCAN_CODES - Get 2 scan codes from a key name.
ASSIGN_CMD      - Assigns a valid key value to a command.
UNASSKEY        - Creates a list of unnassigned keys.
BUILDKEY        - Converts a keystroke macro to a macro language statement.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/


#include mesys.sh

global
{
	int _reset_screen;
}

/******************** Multi-Edit VOID Macro Function ************************

 NAME:         setup()

 DESCRIPTION:  The main setup dialog box

 PARAMETERS:   none

 RETURNS:      nothing

*****************************04-22-93 10:49am*******************************/
void setup()
{
	int menu = menu_create;
	int cpux = cpu, video_cardx = video_card;
	int start_num = 1, tw = window_id, tr = refresh;
	int tundo = undo_stat;


	undo_stat = false;
	refresh = false;
	if(video_cardx > 11)
		video_cardx = 100;
	if( cpux > 4 )
		cpux = 5;

	call check_keymap;

	do
	{
		_reset_screen = 0;
		if((dos_version & 0x00ff) >= 10)
		{
			return_str = "OS/2 " + STR((DOS_VERSION & $00FF) / 10) + '.' + STR(DOS_VERSION >> 8);
		}
		else
		{
			return_str = "DOS " + STR(DOS_VERSION & $00FF) + '.' + STR(DOS_VERSION >> 8);
		}
		menu_set_item(menu, 1, "System: " + return_str
					+ ", " + parse_str("/" + str(cpux) + "=",
									"/0=8086//88/1=80186//88/2=80286/3=80386/4=80486/5=cpu unknown") +
					", " + parse_str("/" + str(video_cardx) + "=",
		'/0=CGA/1=IBM Mono/2=MONROE Color/3=MONROE Mono/4=SIGMA C400/5=EGA Color/6=Hercules Mono/7=VGA Mono/8=VGA Color/9=EGA Mono/10=Wyse 700/11=ATI EGA Wndr/100=Custom video'
						 ), "","/L=1/C=23", 10, 0, 0 );

		menu_set_item(menu, 2, "Serial #: "+Global_Str('@ME_SERIAL#') ,"", "/L=2/C=23", 10, 0, 0);
		if(user_id  != "" )
		{
			return_str = "  User ID: " + user_id;
		}
		else
		{
			return_str = "";
		}
		menu_set_item(menu, 3, "Licensed users: "+str(max_user_count) + return_str,"", "/L=3/C=23", 10, 0, 0);
		menu_set_item(menu, 4, "ME Path: " + me_path , "", "/L=4/C=23", 10, 0, 0);

		menu_set_item(menu, 5, "Edit settings      ", "SETEDIT", "/QK=1/L=1/C=1/R=10/M=1", 11, 0, 0 );
		menu_set_item(menu, 6, "Display            ", "DISPLAY_SETUP /RS=1", "/QK=1/L=3/C=1/R=10/M=1", 11, 0, 0 );
		menu_set_item(menu, 7, "Colors             ", "SETCOLOR /RS=1", "/QK=1/L=5/C=1/R=10/M=1", 11, 0, 0 );
		menu_set_item(menu, 8, "Key mapping        ", "SETKEYS", "/QK=1/L=7/C=1/R=10/M=1", 11, 0, 0 );
		menu_set_item(menu, 9, "Mouse / Key repeat ", "SETMOUSE", "/QK=1/L=9/C=1/R=10/M=1", 11, 0, 0 );
		menu_set_item(menu,10, "Filename extensions", "EXTENS", "/QK=1/L=11/C=1/R=10/M=1", 11, 0, 0 );
		menu_set_item(menu,11, "Printer            ", "PRTTYPE", "/QK=1/L=13/C=1/R=10/M=1", 11, 0, 0 );
		menu_set_item(menu,12, "Swapping / Memory  ", "SETEMS", "/QK=2/L=15/C=1/R=10/M=1", 11, 0, 0 );
		menu_set_item(menu, 13, "Backups / Temp files / Autosave", "SETMEPATHS", "/QK=1/L=7/C=23/R=10/M=1", 11, 0, 0 );
		menu_set_item(menu, 14, "Search and Replace defaults    ", "SRCHSET", "/QK=1/L=9/C=23/R=10/M=1", 11, 0, 0 );
		menu_set_item(menu, 15, "User interface settings        ", "InterfaceSetup", "/QK=1/L=11/C=23/R=10/M=1", 11, 0, 0 );
		menu_set_item(menu, 16, "File compare settings          ", "FC^FC_CONFIG", "/QK=3/L=13/C=23/R=10/M=1", 11, 0, 0 );
		menu_set_item(menu, 17, "Module installation            ", "MODINST", "/QK=8/L=15/C=23/R=10/M=1", 11, 0, 0 );
		menu_set_item(menu, 18, "Save settings", "", "/QK=3/L=18/C=3/R=1", 11, 0, 0 );
		menu_set_item(menu, 19, "Done", "", "/L=18/KC=<ESC>/K1=27/K2=1/C=20/R=0", 11, 0, 0 );
		menu_set_item(menu, 20, "Help", "", "/L=18/KC=<F1>/K2=59/C=45/R=2", 11, 0, 0 );

		return_int = menu;
		rm("Data_In /HN=1/#=20/T=INSTALLATION AND SETUP/A=1/ABT=Done/H=IN/S=" + str(start_num));
		if( return_int == 1 )
		{
			rm("SetSave");
		}
		else if( _reset_screen )
		{
			if(_reset_screen == 1)
				rm("SetScrn");
			refresh = true;
			new_screen;
			refresh = false;
			start_num = Global_Int("!DATA_IN_#");
		}
		else
		{
	 //		rm("Check_Setup");
		}
	}
	while( _reset_screen );

	menu_delete( menu );
	switch_win_id(tw);
	undo_stat = tundo;
	refresh = tr;
	return();

check_keymap:

#ifdef DEMO
  RET;
#else
/* Check to see if the keymap DB file has a later date than INIT.MAC
	 and if so, set the SETUP_CHANGED global flag.
*/
	int jx = file_search_attr;
	Return_Str = Parse_Str("FN=",Global_Str('@KEYMAP_NAME@')) + ".DB";
	if (Return_Str != "") {
		RM("MAKEUSERPATH");
		if (!file_exists(return_str)) {
// If the user specific keymap file does not exist, set the flag.
			RM("MAKEUSERPATH /DF=1");   // create it and set the flag
			goto SET_CHANGED_FLAG;
		}
		File_Search_Attr = 0;
		if (First_File(Return_Str) == 0) {
			int LFD = Last_File_Time;
			if (First_File(GetUserMacroPath + User_Id + 'INIT.MAC') == 0) {
/* If so, consider it a change to the keymap */
				if (Last_File_Time < LFD) {
					goto SET_CHANGED_FLAG;
				}
			}
		}
		else {
SET_CHANGED_FLAG:
			Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $02);
		}
		File_Search_Attr = Jx;
	}
	return_str = User_Id + "KEYMAP.S";
	rm("MakeUserPath");
	if(!file_exists( return_str) ) {
			Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $02);
	}
	ret;
#endif // DEMO
}


macro InterfaceSetup TRANS2 NO_BREAK{
/*******************************************************************************
																MULTI-EDIT MACRO

Name: INTERFACESETUP

Description: A dialog box to configure user interface preferences.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int menu = menu_create;
	int restore_mode = global_int("RESTORE");

	menu_set_item( menu, 1, 'Dialog box style:','',
				 '/C=1/L=1',10,0, 0);
	menu_set_item( menu, 2, 'Multi-Edit Classic ','',
				'/C=2/L=2/QK=1',12,Global_Int('@DIALOG_STYLE') == 0, 0);
	menu_set_item( menu, 3, 'Windows/SAA(CUA)   ','',
				' /C=2/L=3/QK=1',12,Global_Int('@DIALOG_STYLE') != 0, 0);
	menu_set_item( menu, 4, 'Options:','',
				 '/C=28/L=1',10,0, 0);
	menu_set_item( menu, 5, 'Immediately display dir in file prompts ','',
				'/C=29/L=2/QK=1',13,Global_Int('DIR_IMMEDIATE'), 0);
	menu_set_item( menu, 6, 'Overlap split window borders            ','',
				'/C=29/L=3/QK=1',13,Global_Int('SPLIT_EDGE_MODE') == 0, 0);
	menu_set_item( menu, 7, 'Auto-arrange compiler error window      ','',
				'/C=29/L=4/QK=1',13,Global_Int('!AutoArrangErrSrc'), 0);
	menu_set_item( menu, 8, 'Auto-arrange minimized windows          ','',
				'/C=29/L=5/QK=2',13,Global_Int('AUTO_ARRANGE_ICONS'), 0);
	menu_set_item( menu, 9, 'Persistent blocks                       ','',
				'/C=29/L=6/QK=1',13,persistent_blocks, 0);
	menu_set_item( menu, 10, 'Inclusive stream blocks                 ','',
				'/C=29/L=7/QK=2',13,stream_block_mode, 0);
	menu_set_item( menu, 11, 'Confirm file overwrites                 ','',
				'/C=29/L=8/QK=1',13,!Global_Int("NO_CONFIRM_OVERWRITE"), 0);
	menu_set_item( menu, 12, 'Bypass compiler menus with only 1 entry ','',
				'/C=29/L=9/QK=1',13,Global_Int('@PGM_MENU_BYPASS'), 0);
  menu_set_item(menu, 13,  "Unix-style regular expressions          ", "",
				"/QK=4/L=10/C=29", 13, global_int("REG_EXP_STYLE"), 0 );

	menu_set_item( menu, 14, 'Wildcards in File Prompts:','',
				 '/C=1/L=5',10,0, 0);
	menu_set_item( menu, 15, 'Load wildcards      ','',
				'/C=2/L=6/QK=1',12,Global_Int('@WILDCARD_MODE') == 0, 0);
	menu_set_item( menu, 16, 'Display in dir list ','',
				'/C=2/L=7/QK=1',12,Global_Int('@WILDCARD_MODE') != 0, 0);

	menu_set_item( menu, 17, 'Restore previous status:','',
				'/C=1/L=11/QK=1',10,0, 0);
	menu_set_item( menu, 18, 'No restore              ','',
				'/C=2/L=12',12,restore_mode == 0, 0);
	menu_set_item( menu, 19, 'Status file in each dir ','',
				'/C=2/L=13',12,restore_mode == 1, 0);
	menu_set_item( menu, 20, 'One global status file            ','',
				'/C=31/L=12',12,restore_mode == 2, 0);
	menu_set_item( menu, 21, 'Encoded status files for each dir ','',
				'/C=31/L=13',12,restore_mode == 3, 0);
	menu_set_item( menu, 22, 'Status file path:',global_str("@RESTORE_PATH"),
				'/QK=1/C=1/L=14/W=30/ML=128',0,0, 0);

	return_int = menu;
	RM('USERIN^DATA_IN /HN=1/PRE=D/#=22/T=USER INTERFACE SETUP/H=USERINT');
	if(  return_int  ) {
		Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
		Set_Global_Int('@DIALOG_STYLE',menu_item_int( menu, 3, 2 ));
		Set_Global_Int('DIR_IMMEDIATE', menu_item_int( menu, 5, 2 ) );
		Set_Global_Int('SPLIT_EDGE_MODE', menu_item_int( menu, 6, 2 ) == 0 );
		Set_Global_Int('!AutoArrangErrSrc', menu_item_int( menu, 7, 2 ) );
		Set_Global_Int('AUTO_ARRANGE_ICONS', menu_item_int( menu, 8, 2 ) );
		persistent_blocks = menu_item_int(menu, 9, 2);
		stream_block_mode = menu_item_int(menu, 10, 2);
		Set_Global_Int("NO_CONFIRM_OVERWRITE", !menu_item_int( menu, 11, 2 ));
		if(  (GLOBAL_INT('@DIALOG_STYLE') == 1)  ) {   /* set globals for help system */
			set_global_str('!ACCEPT_LABEL','OK<ENTER>');
			Set_global_str('!ACCEPT_KEY','<ENTER>');
			set_global_str('!NEXT_KEY','<TAB>');
			set_global_str('!PREV_KEY','<ShftTAB>');
			set_global_str('!HISTORY_KEY','<>');
			set_global_str('!GROUP_KEY','<>');
		} else {
			set_global_str('!ACCEPT_LABEL','  OK<F10>');
			Set_global_str('!ACCEPT_KEY','<F10>');
			set_global_str('!NEXT_KEY','<>');
			set_global_str('!PREV_KEY','<>');
			set_global_str('!HISTORY_KEY','<F4>');
			set_global_str('!GROUP_KEY','<|26>');
		}
		Set_Global_Int('@PGM_MENU_BYPASS',menu_item_int( menu, 12, 2 ));
		Set_Global_Int('REG_EXP_STYLE',menu_item_int( menu, 13, 2 ));
		Set_Global_Int('@WILDCARD_MODE',menu_item_int( menu, 16, 2 ));

		// Setup restore options
		restore_mode = 0;
		if( menu_item_int( menu, 19, 2 ))
		{
			restore_mode = 1;
		}
		else if ( menu_item_int( menu, 20, 2 ) )
		{
			restore_mode = 2;
		}
		else if ( menu_item_int( menu, 21, 2 ) )
		{
			restore_mode = 3;
		}
		set_global_int( "RESTORE", restore_mode );
		set_global_str("@RESTORE_PATH", menu_item_str( menu, 22, 2 ) );
	}
	menu_delete( menu );
}


macro CHECK_SETUP TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  CHECK_SETUP

Description:  Checks for configuration changed and prompts the user to save.

Parameters:
							/X=   X coordinate for menu box
							/Y=   Y coordinate for menu box

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

#ifdef DEMO
  Set_Global_Int('SETUP_CHANGED',0);
#else
	int  x, y ;
	X = parse_int( '/X=', mparm_str );
	Y = parse_int( '/Y=', mparm_str );
	if(  x == 0  ) {
		x = 2;
	}
	if(  y == 0  ) {
		y = 2;
	}
	return_int = 1;
	if(  Global_Int('SETUP_CHANGED')  ) {
		rm('xmenu /B=1/T=1/L=Configuration has changed/X=' + str(x) + '/Y=' + str(y) + '/M=Save configuration and exit setup()eXit setup without saving configuration()');
		if(  return_int == 1  ) {
	/* Run the actual code generator and compiler */
			RM( 'SETSAVE' );
			if(  (Return_Str == 'COMP ERROR')  ) {
				return_int = 0;
			} else {
				return_int = 1;
			}
		}
	}
#endif // DEMO

}

macro SETEMS TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  SETEMS

Description:  Menu to configure shell to DOS swapping to ems and/or disk.


							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

		int  menu = menu_create, t_es1, t_es ;

		if(swap_mode == 0)
			swap_mode = 1;
		/* get individual flags(nybbles) by masking */
		t_es = Global_Int('!ADD_MEM_TYPE');
		menu_set_item( menu, 1, '','',
			'/C=1/L=1',10, 0 , 0);
		menu_set_item( menu, 2, 'Swap method (when shelling to dos):','',
			'/QK=1/C=1/L=1',10,0, 0);
		menu_set_item( menu, 3, 'Swap to disk only                  ','',
			'/C=2/L=2',12, (swap_mode == 1) , 0);
		menu_set_item( menu, 4, 'Swap to additional memory(EMS/XMS) ','',
			' /C=2/L=3',12, (swap_mode > 1) , 0);
		menu_set_item( menu, 5, 'Default memory to free up for dos','',
			'/QK=1/C=1/L=6',10,0, 0);
		menu_set_item( menu, 6, ' (64-640 K bytes, 0=All):','',
			'/C=1/W=4/L=7',1, (swap_mem * 16) / 1024 , 0);
		menu_set_item( menu, 7, 'Additional Memory to use:','',
			'/QK=1/C=44/L=1',10,0, 0);
		menu_set_item( menu, 8, 'None                 ','',
			'/C=45/L=2',12, (t_es == 0) , 0);
		menu_set_item( menu, 9, 'EMS(Expanded Memory) ','',
			'/C=45/L=3',12, (t_es == 1) , 0);
		menu_set_item( menu, 10, 'XMS(eXtended Memory) ','',
			'/C=45/L=4',12, (t_es == 2) , 0);
		menu_set_item( menu, 11, 'EMS/XMS to leave available','',
			'/QK=1e/C=44/L=6',10,0, 0);
		menu_set_item( menu, 12, '(0-16000K bytes):','',
			'/C=45/MIN=0/MAX=16000/W=6/L=7',1, global_int( '!ADD_MEM_AMOUNT') , 0);
		menu_set_item( menu, 13, 'Total XMS (upon startup): ' + str( XMS_Total ) + 'K','',
			'/C=3/L=9',10, 0 , 0);
		menu_set_item( menu, 14, 'Total EMS (upon startup): ' + str( EMS_Total ) + 'K','',
			'/C=3/L=10',10, 0 , 0);
		menu_set_item( menu, 15, 'In use for virtual memory: ' + str( XMS_In_Use ) + 'K','',
			'/C=37/L=9',10, 0 , 0);
		menu_set_item( menu, 16, 'In use for virtual memory: ' + str( EMS_In_Use ) + 'K','',
			'/C=37/L=10',10, 0 , 0);
		menu_set_item( menu, 17, 'Modify prompt when shelling to dos','',
			' /C=2/L=12/QK=1',13, global_int("EXEC_ENV_MODE") , 0);
		return_int = menu;
		RM('USERIN^DATA_IN /HN=1/#=17/T=SWAPPING // EMS // XMS USAGE/H=INSHELL');
		if(  return_int  ) {
			swap_mode = 0;
			if(  menu_item_int( menu, 3, 2 )  ) {
				swap_mode = 1;
			} else if(  menu_item_int( menu, 4, 2 )  ) {
				swap_mode = 3;
			}
			Swap_Mem = (menu_item_int( menu, 6, 2 ) * 1024) / 16;

		/* Set global to new value */
			Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
			Set_Global_Int('!ADD_MEM_AMOUNT', menu_item_int( menu, 12, 2 ));
			t_es = 0;
			if(  menu_item_int( menu, 9, 2 )  ) {
				t_es = 1;
			} else if(  menu_item_int( menu, 10, 2 )  ) {
				t_es = 2;
			}
			Set_Global_Int('!ADD_MEM_TYPE', t_es );
			set_global_int("EXEC_ENV_MODE", menu_item_int( menu, 17, 2 ) );
			RM('MEERROR^MESSAGEBOX /M=NOTE:  Changes to EMS//XMS setup will not take effect until after you save the settings, and restart Multi-Edit.');
		}
		menu_delete( menu );
}


/*
ÚÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄSWAPPING / EMS / XMS USAGEÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ·
³Swap method (when shelling to dos):        Additional Memory to use:  º
³  ( ) No swap                                ( ) None                 º
³  ( ) Swap to disk only                      () EMS(Expanded Memory) º
³  () Swap to additional memory(EMS/XMS)     ( ) XMS(eXtended Memory) º
³                                                                      º
³Default memory to free up for dos          EMS/XMS to leave available º
³ (64-640 K bytes, 0=All): 0                 (0-16000K bytes): 0       º
³                                                                      º
³Total XMS upon startup: 2080K   In use for virtual memory: 0K         º
³Total EMS upon startup:    0K   In use for virtual memory: 1456K      º
³                                                                      º
³                  OK<ENTER>Ü Cancel<ESC>Ü Help<F1>Ü                   º
³                   ßßßßßßßßß  ßßßßßßßßßßß  ßßßßßßßß                   º
ÔÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍ¼
*/


#ifndef DEMO
macro SETSERIAL TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  SETSERIAL

Description:  This is the serial number data entry box.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	Return_Str = Global_Str('@ME_SERIAL#');
	RM('USERIN^QUERYBOX /W=15/H=PROMPTS/T=MULTI-EDIT SERIAL NUMBER/P=Enter the serial number from the diskettes:');
	if(  (Return_Int)  ) {
		Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
		Set_Global_Str('@ME_SERIAL#',Return_Str);
	}
}
#endif


macro DISPLAY_SETUP TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  DISPLAY_SETUP

Description:  The display configuration menu

Parameters:
							/BC= Amount of boxes on the screen before this macro is called
									 so it can clean up after it'self.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str  tstr[80] ;
	int  tbc = parse_int('/BC=', mparm_str),
					result = 1,T_Int,
					menu = menu_create,
					st
					;

	menu_set_item( menu, 1, 'Video mode','',
								'/C=1',10,0, 0);
	menu_set_item( menu, 2, '25 lines    ','',
								'/QK=1/C=2',12,(Video_Mode == 0), 0);
	menu_set_item( menu, 3, '30/33 lines ','',
								'/QK=1/C=2',12,(Video_Mode == 1), 0);
	menu_set_item( menu, 4, '43/50 lines ','',
								'/QK=1/C=2',12,(Video_Mode == 2), 0);
	if( ext_video_status == 1 )
		menu_set_item( menu, 5, 'UltraVision ','',
									'/QK=1/C=2',12,(Video_Mode == 3), 0);
	else
		menu_set_item( menu, 5, ' ( ) UltraVision ','','/C=2/ATTR=' + str(m_k_color),10,0,0);
	menu_set_item( menu, 6, 'Exploding boxes ','',
								'/QK=2/C=21/L=1',13,explosions, 0);
	menu_set_item( menu, 7, '<Ctrl> Help box ','',
								'/QK=8/C=21/L=2',13,ctrl_help, 0);
	menu_set_item( menu, 8, 'F-key labels delay(1/10 secs):','',
								'/QK=1/C=21/W=2/MIN=0/MAX=99/L=3',1,(fkey_delay * 55) / 100, 0 );
	menu_set_item( menu, 9, 'UltraVision mode (hex):','',
								'/L=4/C=21/W=3',4, ext_video_mode, 0);

	menu_set_item( menu, 10, 'Screen layouts','',
								'/L=6/C=21',10,0, 0);
	menu_set_item( menu, 11, 'Default layout','',
								'/L=7/C=1',10,0, 0);

	tstr = global_str('@SCREEN_SETA' );

	menu_set_item( menu, 12, 'Layout A               ','',
								'/QK=8/C=2/L=8',12,Global_Int('DEF_SCRN_STYLE') < 2, 0);
	menu_set_item( menu, 13, 'Layout B            ','',
								'/QK=8/C=30/L=8',12,Global_Int('DEF_SCRN_STYLE') == 2, 0);
	st = 14;
	menu_set_item( menu, st, 'Status/message line ','',
								'/QK= /C=2/L=10',13,(parse_int('/S=', tstr) != 0 ), 0);
	menu_set_item( menu, st + 1, 'Menu bar            ','',
								'/QK=/C=2/L=11',13,(parse_int('/MB=', tstr) != 0), 0);
	menu_set_item( menu, st + 2, 'Function key labels ','',
								'/QK=/C=2/L=12',13,(parse_int('/F=', tstr) != 0), 0);
	menu_set_item( menu, st + 3, 'Left-hand border    ','',
								'/QK=/C=2/L=13',13,(parse_int('/LB=', tstr) != 0), 0);
	menu_set_item( menu, st + 4, 'Right-hand border   ','',
								'/QK=/C=2/L=14',13,(parse_int('/RB=', tstr) != 0), 0);
	menu_set_item( menu, st + 5, 'Bottom border       ','',
								'/QK=/C=2/L=15',13,(parse_int('/BB=', tstr) != 0), 0);
	T_Int = xpos('/MACRO=', tstr, 1 );
	if(  T_Int != 0  ) {
		menu_set_item( menu, st + 6, 'Macro:',shorten_str(copy( tstr, T_Int + 7, 40 )),
									'/L=16/C=2/W=15/ML=40',0,0, 0);
	} else {
		menu_set_item( menu, st + 6, 'Macro:','',
									'/L=16/C=2/W=15/ML=40',0,0, 0);
	}
	tstr = global_str('@SCREEN_SETB' );
	menu_set_item( menu, st + 7, 'Status/Message line ','',
								'/QK= /C=30/L=10',13,(parse_int('/S=', tstr) != 0 ), 0);
	menu_set_item( menu, st + 8, 'Menu bar            ','',
								'/QK=/C=30/L=11',13,(parse_int('/MB=', tstr) != 0), 0);
	menu_set_item( menu, st + 9, 'Function key labels ','',
								'/QK=/C=30/L=12',13,(parse_int('/F=', tstr) != 0), 0);
	menu_set_item( menu, st + 10, 'Left-hand border    ','',
								'/QK=/C=30/L=13',13,(parse_int('/LB=', tstr) != 0), 0);
	menu_set_item( menu, st + 11, 'Right-hand border   ','',
								'/QK=/C=30/L=14',13,(parse_int('/RB=', tstr) != 0), 0);
	menu_set_item( menu, st + 12, 'Bottom border       ','',
								'/QK=/C=30/L=15',13,(parse_int('/BB=', tstr) != 0), 0);
	T_Int = xpos('/MACRO=', tstr, 1 );
	if(  T_Int != 0  ) {
		menu_set_item( menu, st + 13, 'Macro:',shorten_str(copy( tstr, T_Int + 7, 40 )),
								'/L=16/C=30/W=15/ML=40',0,0, 0);
	} else {
		menu_set_item( menu, st + 13, 'Macro:','',
									'/L=16/C=30/W=15/ML=40',0,0, 0);
	}

	return_int = menu;
	RM('USERIN^DATA_IN /HN=1/PRE=DS/#=27/H=INDS/T=DISPLAY SETUP');
	if(  return_int  ) {
		Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
		explosions = menu_item_int( menu, 6, 2 );
		ctrl_help = menu_item_int( menu, 7, 2 );
		tstr = '';
		if(  menu_item_int( menu, st, 2 )  ) {
			tstr = tstr + '/S=1';
		}
		if(  menu_item_int( menu, st + 1, 2 )  ) {
			tstr = tstr + '/MB=1';
		}
		if(  menu_item_int( menu, st + 2, 2 )  ) {
			tstr = tstr + '/F=1';
		}
		if(  menu_item_int( menu, st + 3, 2 )  ) {
			tstr = tstr + '/LB=1';
		}
		if(  menu_item_int( menu, st + 4, 2 )  ) {
			tstr = tstr + '/RB=1';
		}
		if(  menu_item_int( menu, st + 5, 2 )  ) {
			tstr = tstr + '/BB=1';
		}
		set_global_str('@SCREEN_SETA', tstr + '/MACRO=' + menu_item_str( menu, st + 6, 2 ) );
		tstr = '';
		if(  menu_item_int( menu, st + 7, 2 )  ) {
			tstr = tstr + '/S=1';
		}
		if(  menu_item_int( menu, st + 8, 2 )  ) {
			tstr = tstr + '/MB=1';
		}
		if(  menu_item_int( menu, st + 9, 2 )  ) {
			tstr = tstr + '/F=1';
		}
		if(  menu_item_int( menu, st + 10, 2 )  ) {
			tstr = tstr + '/LB=1';
		}
		if(  menu_item_int( menu, st + 11, 2 )  ) {
			tstr = tstr + '/RB=1';
		}
		if(  menu_item_int( menu, st + 12, 2 )  ) {
			tstr = tstr + '/BB=1';
		}
		set_global_str('@SCREEN_SETB', tstr + '/MACRO=' + menu_item_str( menu, st + 13, 2 ) );
		if(  menu_item_int( menu,12, 2 )  ) {
			Set_Global_Int('DEF_SCRN_STYLE', 1);
		} else {
			Set_Global_Int('DEF_SCRN_STYLE', 2);
		}

		fkey_delay = (menu_item_int( menu, 8, 2 ) * 182) / 100;
		if(((menu_item_int( menu, 8, 2 ) * 182) % 100) > 0)
				++fkey_delay;
		t_int = 0;
		if( menu_item_int( menu, 3, 2 ))
				t_int = 1;
		else if( menu_item_int( menu, 4, 2 ) )
				t_int = 2;
		else if( menu_item_int( menu, 5, 2 ) )
				t_int = 3;
		if ((t_int != video_mode) || (ext_video_mode != menu_item_int( menu, 9, 2 ))) {
			result = 1000;
			ext_video_mode = menu_item_int( menu, 9, 2 );
			RM('TGLVID /M=' + str(t_int) );
		}
		if( parse_int("/RS=", mparm_str))
		{
			set_global_int("_reset_screen", 1 );
			push_key( 27, 1 );
		}
	}
	return_int = RESULT;
	goto exit;

DO_KILL_BOXES:
	while(  (box_count > tbc)  ) {
		kill_box;
	}
	RET;
exit:
	menu_delete( menu );
}


macro SRCHSET TRANS2 {
/********************************MULTI-EDIT MACRO******************************

Name: SRCHSET

Description:  Setup routine for default search/search and replace/multi-file
							search settings.

Global Strings Modified:
							'DEF_SWITCHES'
							'DEF_REPL_SWITCHES'
							'DEF_FS_SWITCHES'
							'DEF_FS_PATH'
							'DEF_FSR_SWITCHES'
							'DEF_FSR_PATH'

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

		int  /*use_reg_exp, case_sensitive, search_dirs,*/jx,
				menu, menu_count/*, search_in_mem*/ ;
		str  switches_str[40], temp_return, title[40];


		temp_return = return_str;
		jx = 1;
loop:
		menu_count = 5;
		title = "MULTI-FILE SEARCH SETUP";
	/*
		menu = menu_create;
		menu_set_item(menu, 1, "Search                       ", "", "/QK=1/L=1/C=1/R=10", 11, 0, 0 );
		menu_set_item(menu, 2, "search and Replace           ", "", "/QK=12/L=3/C=1/R=11", 11, 0, 0 );
		menu_set_item(menu, 3, "Multi-file search            ", "", "/QK=1/L=5/C=1/R=12", 11, 0, 0 );
		menu_set_item(menu, 4, "multi-File search and replace", "", "/QK=7/L=7/C=1/R=13", 11, 0, 0 );
		menu_set_item(menu, 5, "Unix-style regular expressions", "", "/QK=1/L=9/C=1", 13, global_int("REG_EXP_STYLE"), 0 );
		return_int = menu;
		rm("DATA_IN /HN=1/T=SEARCH DEFAULTS/#=5/H=INEDSE/S=6");
		menu_delete(menu);
		if(return_int >= 10)
			return_int -= 9;
	*/
		RM('XMENU /T=1/B=1/L=SEARCH DEFAULTS SETUP/S=' + str(jx) + '/M=Search defaults(INEDSE)search and Replace defaults()Multi-file search defaults()multi-File search & replace defaults()' +
			'/X=' + parse_str('/X=', mparm_str) + '/Y=' + parse_str('/Y=', mparm_str));

		jx = return_int;
		if(  return_int < 1  ) {
			goto exit;
		} else if(  return_int == 1  ) {
			RM('MEUTIL2^SrchIntr /M=0/SWONLY=1/SWG=DEF_SWITCHES/H=INEDSE' );
			if(  return_int  ) {
				Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
			}
		} else if(  return_int == 2  ) {
			RM('MEUTIL2^SrchIntr /M=1/SWONLY=1/SWG=DEF_REPL_SWITCHES/H=INEDSE' );
			if(  return_int  ) {
				Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
			}
		} else if(  return_int > 2  ) {
			menu = menu_create;
			if (return_int == 3) {
				switches_str = global_str('DEF_FS_SWITCHES');
				menu_set_item( menu, 5, 'Starting path: ',global_str('DEF_FS_PATH'),
							'/QK=10/C=1/L=6/ML=60/W=30',0,0, 0);
			} else {
				switches_str = global_str('DEF_FSR_SWITCHES');
				menu_set_item( menu, 5, 'Leave all files open     ','',
							'/QK=1/C=2/W=1',13,xpos('O',switches_str, 1) != 0, 0);
				menu_set_item( menu, 6, 'Starting path: ',global_str('DEF_FSR_PATH'),
							'/QK=10/C=1/L=7/ML=60/W=30',0,0, 0);
				menu_count = 6;
				title = "MULTI-FILE SEARCH AND REPLACE SETUP";
			}
			menu_set_item( menu, 1, 'Search sub-directories   ','',
						'/QK=12/C=2/W=1',13,xpos('S',switches_str, 1) != 0, 0);
			menu_set_item( menu, 2, 'Case Sensitivity         ','',
						'/QK=1/C=2/W=1',13,xpos('I',switches_str, 1) == 0, 0);
			menu_set_item( menu, 3, 'Regular Expressions      ','',
						'/QK=9/C=2/W=1',13,xpos('X',switches_str, 1) == 0, 0);
			menu_set_item( menu, 4, 'search files in Memory   ','',
						'/QK=17/C=2/W=1',13,xpos('F',switches_str, 1) != 0, 0);
/*
			use_reg_exp = xpos('R',switches_str, 1) != 0;
			case_sensitive = xpos('C',switches_str, 1) != 0;
			search_dirs = xpos('S',switches_str, 1) != 0;
			search_in_mem = xpos('F',switches_str, 1) != 0;
			menu_set_item( menu, 1, 'Search sub-directories   ','',
						'/QK=12/C=2/W=1',13,search_dirs, 0);
			menu_set_item( menu, 2, 'Case Sensitivity         ','',
						'/QK=1/C=2/W=1',13,case_sensitive, 0);
			menu_set_item( menu, 3, 'Regular Expressions      ','',
						'/QK=9/C=2/W=1',13,use_reg_exp, 0);
			menu_set_item( menu, 4, 'search files in Memory   ','',
						'/QK=17/C=2/W=1',13,search_in_mem, 0);
			menu_set_item( menu, 5, 'Starting path: ',global_str('DEF_FS_PATH'),
						'/QK=10/C=1/L=6/ML=60/W=30',0,0, 0);
*/
			return_int = menu;
			RM('UserIn^Data_In /HN=1/PRE=SR/H=INEDSE/S=1/#=' + str(menu_count) +
				'/T=' + title );
			if(  return_int  ) {
				switches_str = '';
				if(!menu_item_int( menu, 3, 2 )) {
					switches_str = 'X';
				}
				if(!menu_item_int( menu, 2, 2 )) {
					switches_str = switches_str + 'I';
				}
				if(menu_item_int( menu, 1, 2 )) {
					switches_str = switches_str + 'S';
				}
				if(menu_item_int( menu, 4, 2 )) {
					switches_str = switches_str + 'F';
				}
				if (menu_count == 6) {
					if(menu_item_int( menu, 5, 2 )) {
						switches_str = switches_str + 'O';
					}
					set_Global_Str('DEF_FSR_SWITCHES', switches_str);
					set_global_str('DEF_FSR_PATH', menu_item_str(menu, 6, 2));
				} else {
					set_Global_Str('DEF_FS_SWITCHES', switches_str);
					set_global_str('DEF_FS_PATH', menu_item_str(menu, 5, 2));
				}
/*
				search_dirs = menu_item_int( menu, 1, 2 );
				set_global_str('DEF_FS_PATH', menu_item_str(menu, 5, 2));
				case_sensitive = menu_item_int( menu, 2, 2 );
				use_reg_exp = menu_item_int( menu, 3, 2 );
				search_in_mem = menu_item_int( menu, 4, 2 );
				switches_str = '';
				if(  use_reg_exp  ) {
					switches_str = 'R';
				}
				if(  case_sensitive  ) {
					switches_str = switches_str + 'C';
				}
				if(  search_dirs  ) {
					switches_str = switches_str + 'S';
				}
				if(  search_in_mem  ) {
					switches_str = switches_str + 'F';
				}
				set_Global_Str('DEF_FS_SWITCHES', switches_str);
*/

				Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
			}
			menu_delete( menu );
		}
		goto loop;
exit:
		return_str = temp_return;
}

macro SETEDIT TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  SETEDIT

Description:  This is the edit settings menu.

Parameters:
							/X=   X coordinate for menu box
							/Y=   Y coordinate for menu box

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
/* Global variables used:
Global_Int('Default_Insert_Mode') The insert mode upon startup of editor
Global_Str('Def_Switches') The search switches upon startup of editor
Global_Str('Format_Code_Delimit') The print formatter code delimiter
 */
	int  x1, menu = menu_create ;

	menu_set_item( menu, 1, 'Page break string........', page_str,
							'/QK=1/C=1/W=10',0,0, 0);

/* Specchar and dlbparen properly represent unprintable ASCII codes in the menu */
	RM( 'userin^SPECCHAR ' + Word_Delimits );
	menu_set_item( menu, 2, 'Word delimits............', return_str,
							'/QK=1/C=1/W=40/ML=40',0,0, 0);

	menu_set_item( menu, 3, 'Print formatter code.....', Global_Str('Format_Code_Delimit'),
							'/QK=7/C=1/W=1/ML=1',0,0, 0);

	menu_set_item( menu, 4, 'Max undo count...........','',
							'/QK=5/C=1/W=5/H=INEDMA/MIN=0/MAX=65535',1, max_undo, 0);

	menu_set_item( menu, 5, 'Default file extension(s)', Global_Str('@DEFAULT_EXT_LIST'),
							'/QK=14/C=1/W=40/ML=80/H=INEDDF',0,0, 0);

	menu_set_item( menu, 6, 'Cursor:', '',
							'/C=6/L=6',10,0, 0);
	menu_set_item( menu, 7, 'Insert', '',
							'/QK=1/C=2/L=7',10,0, 0);
	menu_set_item( menu, 8, '- ','',
							'/C=1/L=8',12, (ins_cursor == 0), 0);
	menu_set_item( menu, 9, '- ','',
							'/C=1/L=9',12, (ins_cursor == 1), 0);
	menu_set_item( menu, 10, '- ','',
							'/C=1/L=10',12, (ins_cursor == 2), 0);
	menu_set_item( menu, 11, '- ','',
							'/C=1/L=11',12, (ins_cursor == 3), 0);
	menu_set_item( menu, 12, 'Overwrite', '',
							'/QK=1/C=10/L=7',10,0, 0);
	menu_set_item( menu, 13, 'Underline  _ ','',
							'/C=9/L=8',12, (ovr_cursor == 0), 0);
	menu_set_item( menu, 14, '1/2 block  Ü ','',
							' /C=9/L=9',12, (ovr_cursor == 1), 0);
	menu_set_item( menu, 15, '2/3 block  Ü ','',
							' /C=9/L=10',12, (ovr_cursor == 2), 0);
	menu_set_item( menu, 16, 'Full block Û ','',
							'/C=9/L=11',12, (ovr_cursor == 3), 0);

	menu_set_item( menu, 17, 'Options:', '',
							'/C=35/L=7',10,0, 0);
	menu_set_item( menu, 18, 'Truncate spaces         ','',
							'/QK=4/C=29/L=8',13, truncate_spaces, 0);
	menu_set_item( menu, 19, 'Control-Z at EOF        ','',
							'/QK=9/C=29/L=9',13, ctrl_z, 0);
	menu_set_item( menu, 20, 'CR/LF at EOF            ','',
							'/QK=4 /C=29/L=10',13, eof_cr, 0);
	menu_set_item( menu, 21, 'Edit binary files in Hex','',
							'/QK=24/C=29/L=11',13, global_int("BINARY_MODE"), 0);

	menu_set_item( menu, 22, 'Tab expand:', '',
							'/QK=1/C=60/L=7',10,0, 0);
	menu_set_item( menu, 23, 'Tabs   ','',
							'/C=60/L=8',12, tab_expand, 0);
	menu_set_item( menu, 24, 'Spaces ','',
							'/C=60/L=9',12, (tab_expand == 0), 0);

	menu_set_item( menu, 25, 'Column block move:', '',
							'/QK=1/C=1/L=13',10,0, 0);
	menu_set_item( menu, 26, 'Deletes space     ','',
							'/C=1/L=14',12, column_move_style, 0);
	menu_set_item( menu, 27, 'Leaves space      ','',
							'/C=1/L=15',12, (column_move_style == 0), 0);

	menu_set_item( menu, 28, 'Default mode:', '',
							'/QK=1/C=26/L=13',10,0, 0);
	menu_set_item( menu, 29, 'Insert    ','',
							'/C=26/L=14',12, Global_Int('Default_Insert_Mode'), 0);
	menu_set_item( menu, 30, 'Overwrite ','',
							'/C=26/L=15',12, (Global_Int('Default_Insert_Mode') == 0), 0);

	menu_set_item( menu, 31, 'Leave cursor at:','',
							'/C=43/L=13/QK=4',10,0,0);
	menu_set_item( menu, 32, 'beginning of pasted text','',
							'/C=43/L=14',12,(global_int('CURSOR_EOB') == 0),0);
	menu_set_item( menu, 33, 'end of pasted text      ','',
							'/C=43/L=15',12,(global_int('CURSOR_EOB') != 0),0);

	return_int = menu;
	RM('USERIN^DATA_IN /HN=1/PRE=SE/#=33/T=EDIT SETTINGS/H=INED');


	if(  return_int  ) {
		Page_Str = menu_item_str( menu, 1, 2 );
		RM('USERIN^VALCHAR ' + menu_item_str( menu, 2, 2 ) );
		word_delimits = return_str;
		Set_Global_Str('Format_Code_Delimit', menu_item_str( menu, 3, 2 ));
		max_undo = menu_item_int( menu, 4, 2 );
		Set_Global_Str('@DEFAULT_EXT_LIST', menu_item_str( menu, 5, 2 ));
		if(  menu_item_int( menu, 8, 2 )  ) {
			ins_cursor = 0;
		} else if(  menu_item_int( menu, 9, 2 )  ) {
			ins_cursor = 1;
		} else if(  menu_item_int( menu, 10, 2 )  ) {
			ins_cursor = 2;
		} else if(  menu_item_int( menu, 11, 2 )  ) {
			ins_cursor = 3;
		}

		if(  menu_item_int( menu, 13, 2 )  ) {
			ovr_cursor = 0;
		} else if(  menu_item_int( menu, 14, 2 )  ) {
			ovr_cursor = 1;
		} else if(  menu_item_int( menu, 15, 2 )  ) {
			ovr_cursor = 2;
		} else if(  menu_item_int( menu, 16, 2 )  ) {
			ovr_cursor = 3;
		}

		Truncate_Spaces = menu_item_int( menu, 18, 2 );
		ctrl_z = menu_item_int( menu, 19, 2 );
		eof_cr = menu_item_int( menu, 20, 2 );
		set_global_int("BINARY_MODE", menu_item_int(menu,21,2));
		tab_expand = menu_item_int( menu, 23, 2 );
		column_move_style = menu_item_int( menu, 26, 2 );
		Set_Global_Int('Default_Insert_Mode', menu_item_int( menu, 29, 2 ));
		Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
		set_global_int('CURSOR_EOB', menu_item_int(menu,33,2));
		set_global_int("BINARY_MODE", menu_item_int(menu,21,2));
	}
	menu_delete( menu );
}

macro SETMEPATHS TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  SETMEPATHS

Description:  This menu sets up the backup methods, and backup and
	temp file paths

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  x1, menu = menu_create ;
/* Build a menu with current settings */
	menu_set_item( menu, 1, 'Backup method:','',
													'/QK=1/C=1',10,0, 0);

	menu_set_item( menu, 2, 'Off                           ','',
													'/C=2/L=2',12, (backups & $0F) == 0 , 0);
	menu_set_item( menu, 3, 'Create .BAK file              ','',
													'/C=2/L=3',12, (backups & $0F) == 1 , 0);
	menu_set_item( menu, 4, 'Copy file to backup directory ','',
													'/C=2/L=4',12, (backups & $0F) == 2 , 0);

	menu_set_item( menu, 5, 'Backup Frequency:','',
													'/QK=8/C=40/L=1',10,0, 0);

	menu_set_item( menu, 6, 'First save only ','',
													'/C=41/L=2',12, (backups & $80) == 0 , 0);
	menu_set_item( menu, 7, 'Every save      ','',
													'/C=41/L=3',12, (backups & $80) != 0 , 0);
	menu_set_item( menu, 8, 'Backup Directory:', Global_Str('~BACKUP_PATH') ,
													'/QK=8/L=5/C=1/W=25/ML=80',0,0, 0);
	menu_set_item( menu, 9, 'Temporary file directory:', Global_Str('~TEMP_PATH') ,
													'/QK=1/L=7/C=1/W=25/ML=80',0,0, 0);
	menu_set_item( menu, 10, 'Autosave setup:','',
													'/QK=1/C=1/L=10',10,0, 0);
	menu_set_item( menu, 11, 'Seconds of keyboard inactivity (0 = OFF)..  ','',
													'/C=3/W=3/L=11/MIN=0/MAX=254',1, parse_int('/T1=', global_str('!AUTOSAVEPARMS')) , 0);
	menu_set_item( menu, 12, 'Save complete editor status as well as files','/T=YES/F=NO',
													'/C=3/L=12/W=3',5, parse_int('/S1=', global_str('!AUTOSAVEPARMS')) , 0);
	menu_set_item( menu, 13, 'Absolute interval in seconds (0 = OFF)..    ','',
													'/C=3/W=3/L=13/MIN=0/MAX=999',1, parse_int('/T2=', global_str('!AUTOSAVEPARMS')) , 0);
	menu_set_item( menu, 14, 'Save complete editor status as well as files','/T=YES/F=NO',
													'/C=3/L=14/W=3',5, parse_int('/S2=', global_str('!AUTOSAVEPARMS')) , 0);

	menu_set_item( menu, 15, 'Network file locking','',
													'/QK=1/C=1/L=16',13, file_locking , 0);
	return_int = menu;
	RM('USERIN^DATA_IN /HN=1/T=Backups // Temp files // Autosave/#=15/H=INEDAU');

	if(  return_int  ) {
		file_locking = menu_item_int( menu, 15, 2 );
		Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
		RM('SetAutoSave /M2=1/T1=' + str( menu_item_int( menu, 11, 2 )) +
				'/S1=' + str( menu_item_int( menu, 12, 2 )) +
				'/T2=' + str( menu_item_int( menu, 13, 2 )) +
				'/S2=' + str( menu_item_int( menu, 14, 2 )) +
				'/H1=' + parse_Str('/H1=', global_str('!AUTOSAVEPARMS')) +
				'/H2=' + parse_Str('/H2=', global_str('!AUTOSAVEPARMS')));
		backups = 0;
		if(  menu_item_int( menu, 3, 2 )  ) {
			backups = 1;
		}
		if(  menu_item_int( menu, 4, 2 )  ) {
			backups = 2;
		}
		if(  menu_item_int( menu, 7, 2 )  ) {
			backups = backups | $80;
		}
		return_str = menu_item_str( menu, 8, 2 );
		Set_Global_Str('~BACKUP_PATH', return_str );
		rm('XLATECMDLINE');
		backup_path = return_str;
		if(  ((Length(Backup_Path) > 0) &
				(Copy(Backup_Path,Length(Backup_Path),1) != '\'))  ) {
			Backup_Path = Backup_Path + '\';
		}
		return_str = menu_item_str( menu, 9, 2 );
		Set_Global_Str('~TEMP_PATH', return_str );
		rm('XLATECMDLINE');
		temp_path = return_str;
		if(  ((Length(Temp_Path) > 0) &
				(Copy(Temp_Path,Length(Temp_Path),1) != '\'))  ) {
			Temp_Path = Temp_Path + '\';
		}
	}
	menu_delete( menu );
}


macro SETMOUSE TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  SETMOUSE

Description:  This is the mouse settings menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str  tstr[80] ;
	int  sensitivity, jx, ct, ca, ma, mc ;
	int  speed, keydelay, menu = menu_create ;

	tstr = global_str('@MOUSEPARMS');
	sensitivity = parse_int('/S=', tstr );
	ct = parse_int('/CT=', tstr );
	ca = parse_int('/CA=', tstr );
	ma = parse_int('/MA=', tstr );
	mc = parse_int('/MC=', tstr );
	menu_set_item( menu, 1, 'Enable mouse                                ','',
				'/QK=1/C=1/W=4',13, parse_int('/M=', tstr), 0);
	menu_set_item( menu, 2, 'Turn mouse cursor off when keyboard is used ','',
				'/QK=1/C=1/W=4',13, mou_disappear, 0);


	menu_set_item( menu, 3, 'mouse cursor Type','',
				'/L=4/QK=14/C=1',10,0, 0);
	menu_set_item( menu, 4, 'Transparent       ','',
				'/L=5/C=2',12, (ct == 0), 0);
	menu_set_item( menu, 5, 'Fixed character...','',
				'/L=6/C=2',12, (ct == 1), 0);
	menu_set_item( menu, 6, '','',
				'/L=6/MIN=0/MAX=255/C=24/W=2',4, mc , 0);
	menu_set_item( menu, 7, 'mouse cursor Attributes','',
				'/L=4/QK=14/C=29',10,0, 0);
	menu_set_item( menu, 8, 'Reversed          ','',
				'/L=5/C=30',12, (ca == 0), 0);
	menu_set_item( menu, 9, 'Unchanged         ','',
				'/L=6/C=30',12, (ca == 1), 0);
	menu_set_item( menu, 10, 'Fixed attribute...','',
				'/L=7/C=30',12, (ca == 2), 0);
	menu_set_item( menu, 11, '','',
				'/MIN=0/MAX=255/L=7/C=52/W=2',4, ma , 0);
	menu_set_item( menu, 12, 'Mouse sensitivity [0(Default), 1(slow)..100(fast)]...','',
				'/QK=2/MIN=0/MAX=100/L=9/C=1/W=4',1, sensitivity , 0);

	menu_set_item( menu, 13, 'Key repeat speed (0 = fastest, 31 = slowest):','',
				 '/QK=1/C=1/L=13/W=3',1, Global_Int('KEYSPEED') , 0);
	menu_set_item( menu, 14, 'Key repeat delay (0 = least, 3 = most):','',
				 '/QK=2/C=1/L=14/W=3',1, Global_Int('KEYDELAY') , 0);
	menu_set_item( menu, 15, 'ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄKEY REPEAT SETUPÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ','',
				 '/C=1/L=11',10,0, 0);
	return_int = menu;
	RM( 'USERIN^Data_In /HN=1/H=INMO/#=15/S=1/T=MOUSE SETUP' );
	if(  return_int  ) {
		Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
		if(  mouse != menu_item_int( menu, 1, 2 )  ) {
			mouse = menu_item_int( menu, 1, 2 );
		}
		mou_disappear = menu_item_int( menu, 2, 2 );
		if(  (sensitivity != menu_item_int( menu, 12, 2 ))  ) {
			sensitivity = menu_item_int( menu, 12, 2 );
			if(  mou_installed & (sensitivity != 0)  ) {
				r_bx = sensitivity;
				r_cx = sensitivity;
				r_dx = sensitivity;
				r_ax = 26;
				intr($33);
			}
		}
		ct = menu_item_int( menu, 5, 2 );
		if(  menu_item_int( menu, 9, 2 )  ) {
			ca = 1;
		} else if(  menu_item_int( menu, 10, 2 )  ) {
			ca = 2;
		} else {
			ca = 0;
		}
		ma = menu_item_int( menu, 11, 2 );
		mc = menu_item_int( menu, 6, 2 );
		set_global_str('@MOUSEPARMS', '/S=' + str(sensitivity) + '/M=' + str(menu_item_int( menu, 1, 2 )) +
													'/CT=' + str(ct) + '/CA=' + str(ca) + '/MA=' + str(ma) + '/MC=' + str(mc) );
		if(  mou_installed  ) {
			if(  ct == 0  ) {
				mc = 0;
			}
			RM('MOUTYPE /C=' + str(mc) + '/M=' + str(ca) + '/A=' + str(ma) );
		}
		speed = menu_item_int( menu, 13, 2 );
		keydelay = menu_item_int( menu, 14, 2 );

		if(  (Speed < 0) | (speed > 31) | (keydelay < 0) | (keydelay > 3)  ) {
			RM('MEERROR^Beeps /C=1');
			goto exit;
		}
	/* Set up the registers and do the interrupt */
		if(  (Global_Int('NO_KEYSPEED') == 0)  ) {
			R_BX = (keydelay << 8) | speed;
			R_AX = $0305;
			INTR( $16 );
		}
	/* Save the new current values into global variables */
		Set_Global_Int('KEYSPEED', speed);
		Set_Global_Int('KEYDELAY', keydelay);

	}
	return_int = 0;

exit:
	menu_delete( menu );
}

macro PRTTYPE TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

NAME:  PRTTYPE

DESCRIPTION:  This is the printer setup menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
AGAIN:
	Set_Global_Str('PBTN1','/T=Edit/KC=<F3>/K2=61/W=8/R=11/TP=11');
	RM('USERIN^DB /EBN=PBTN/EBC=1/LO=1/NE=1/NC=1/F=PRINTERS' +
			'/Y=3/ABT=Select/CBT=Done/DPT=PRINTERS.DB/PROTECT1=NONE/PRE=PRT' +
			'/LT=PRINTER TYPE/NOALPHA=1/H=INPR/GLO=PRT' +
			'/DELMAC=1/CANMAC=1/MACRO=CHECK_PRT');
	if(  (Return_Int == 11)  ) {
/* Edit was selected */
		RM('PRINTER_DRIVER');
		Goto AGAIN;
	} else if(  ((Return_Int != 0) & (Return_Str != Global_Str('PRINTER_TYPE')))  ) {
		Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
		Set_Global_Str('PRINTER_TYPE',Return_Str);
	}
	Set_Global_Str('PBTN1','');
}

macro CHECK_PRT TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

NAME:  CHECK_PRT

DESCRIPTION:  Checks to see if a printer type create is aborted or a printer
type is deleted, and if so, finds and deletes any associated printer driver.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str
			tstr[25] = Parse_Str('PT=',Global_Str(Parse_Str('/GLO=',Mparm_Str)));

	if(  (((Parse_Int('/C=',MParm_Str) != 0) &
			(Parse_Int('/CREATE=',MParm_Str) != 0)) |
			(Parse_Int('/D=',MParm_Str) != 0))  ) {
		Mark_Pos;
		Tof;
		Reg_Exp_Stat = True;
		if(  (Search_Fwd('%|12' + Tstr + '.PRT$',0))  ) {
MORE_LINES:
			Del_Line;
			if(  ((At_Eof == False) & (Cur_Char != '|12'))  ) {
				Goto MORE_LINES;
			}
		}
		Goto_Mark;
	}

}

macro PRINTER_DRIVER TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  PRINTER_DRIVER

Description:  Menu of printer codes for the selected printer type

Parameters:
							Return_Str= Printer type.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	RM('USERIN^DB /PRE=PD/HPT=NONE.PRT/F=PRINTERS/CBT=Done/NOALPHA=1/H=INPR/DPT='
			+ Return_Str + '.PRT/LT=PRINTER DRIVER SETUP FOR ' + Return_Str +
			'/PROTECT1=Initialization String/PROTECT2=Termination String');
}

macro EXTENS TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

NAME:  EXTENS

DESCRIPTION:  This is the filename extension specific setup menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	Set_Global_Str('T_Format','');
	RM('USERIN^DB /NDF=/F=MECONFIG/CBT=Done/DPT=EXT.DB/PROTECT1=DEFAULT/PRE=EXT/LT=EXTENSION SETUP/NOALPHA=1/H=FE/GLO=EXT/CP=Input new extension: /DELMAC=1/CANMAC=1/MACRO=CHECK_EXT');
	Set_Global_Str('T_Format','');
	Set_Global_Str("@T_EXT","");
}

macro CHECK_EXT TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

NAME:  CHECK_EXT

DESCRIPTION:  Checks to see if a extension create is aborted or an extension
is deleted, and if so, finds and deletes any associated compiler records.
Also, resets the extension global string of an edited extension record.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str
			g_str[20] = Parse_Str('/GLO=',Mparm_Str),
			tstr[80] = Parse_Str('EXT=',Global_Str(g_str)),
			tstr2[8],
			language[40] = Parse_Str('LS=',Global_Str(g_str));
	int
		t_int,
		t_int2,
		t_int3,
		reinit = false,
		reinit_count,
		language_not_changed,
		active_window = window_id;
		mark_pos;

	if (Parse_Int('/P=',MParm_Str)) {
/* prompt user if he wants to reinitialize extension parameters for all
	 existing windows */
		language_not_changed = (language == parse_str("/LANG=",global_str("@T_EXT")));

			// If there are no files loaded with the designated extensions
			// then do not prompt the user
		t_int2 = 1;
		do
		{
			t_int = xpos(' ', tstr, t_int2 );
			if(t_int == 0)
			{
				tstr2 = copy( tstr,t_int2,3);
			}
			else
			{
				tstr2 = copy( tstr, t_int2, t_int - t_int2 );
			}
			if(global_str("." + caps(tstr2)) != "")
			{
				reinit = TRUE;
				t_int = 0;
			}
			t_int2 = t_int + 1;
		} while( t_int != 0);

		if(reinit)
			RM("VERIFY /H=FE/T=Reset extension configuration in currently loaded files?/BL=");
		reinit = return_int;
		if ((tstr != parse_str("/EXT=",global_str("@T_EXT"))) &&
				(!parse_int("/CREATE=",global_str("@T_EXT")))) {
			Mark_Pos;
			Tof;
			Reg_Exp_Stat = True;
			if (Search_Fwd('%|12' + parse_str("/EXT=",global_str("@T_EXT")) + '.PGM$',0)) {
				right;
				del_chars(length(parse_str("/EXT=",global_str("@T_EXT"))));
				insert_mode = true;
				text(tstr);
			}
			goto_mark;
		}

/* This will reassign the extension specific global on the fly */
		t_int2 = 1;
		call SKIP_DELIMITS;

PARSE_EXT:
		tstr2 = '';
		while ((t_int2 <= svl(tstr)) && (!xpos(str_char(tstr,t_int2)," ;,",1))) {
			tstr2 = tstr2 + str_char(tstr,t_int2);
			++t_int2;
		}
		call SKIP_DELIMITS;
		TStr2 = '.' + Tstr2;
		if (tstr2 == ".DEFAULT") {
/* The default get special treatment.  We need to find all extension specific
globals that use default and reassign them */
			t_int = 1;
FIND_EXT:
			if (t_int3 = XPos("",global_str("EXTENSIONS"),t_int + 1)) {
				tstr2 = copy(global_str("EXTENSIONS"),t_int,t_int3 - t_int);
				if (xpos("DEFAULT",parse_str("EXT=",Global_Str(tstr2)),1)) {
					call SET_EXT_PARMS;
				}
				t_int = t_int3 + 1;
				Goto FIND_EXT;
			}
		} else if (Global_Str(TStr2) != '') {
			call SET_EXT_PARMS;
		}
		if (t_int2 <= svl(tstr))
			goto PARSE_EXT;
	} else {
		Set_Global_Str("@T_EXT","/EXT=" + tstr + "/LANG=" + language + mparm_str);
/*
		if(  (((Parse_Int('/C=',MParm_Str) != 0) &
				(Parse_Int('/CREATE=',MParm_Str) != 0)) |
				(Parse_Int('/D=',MParm_Str) != 0))  ) {
			Mark_Pos;
			Tof;
			Reg_Exp_Stat = True;
			if(  (Search_Fwd('%|12' + Tstr + '.PGM$',0))  ) {
MORE_LINES:
				Del_Line;
				if(  ((At_Eof == False) & (Cur_Char != '|12'))  ) {
					Goto MORE_LINES;
				}
			}
			Goto_Mark;
		}
*/
	}
	goto EXIT;

SKIP_DELIMITS:
	while ((t_int2 <= svl(tstr)) && (xpos(str_char(tstr,t_int2)," ;,",1)))
		++t_int2;
	RET;

SET_EXT_PARMS:
	Set_Global_Str(tstr2,Global_Str(g_str));
	if (reinit) {
/* go around the window list to see if there are any currently loaded files
	 with this extension */
			for (reinit_count = 0;reinit_count < window_count;++reinit_count) {
				switch_window(cur_window + 1);
				if ("." + get_extension(file_name) == tstr2) {
// if the language has not changed, EXTSETUP need not run SET_SYNTAX_PARMS
					RM("EXTSETUP /NOSYNTAX=" + str(language_not_changed));
				}
			}
	}
	RET;

EXIT:
	switch_win_id(active_window);
	goto_mark;
}

macro LANG_MENU TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  LANG_MENU

Description:  Menu of language types for extension setup

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
// GET_DB_RECORD stores the initial amount of records in global_int("@LANG")
	int active_window = window_id;
	mark_pos;

	RM("GET_DB_RECORD /NDF=1/F=MECONFIG/DPT=LANGUAGE.DB/DBF=LANG/AGLO=@LANG/#=1");
	switch_win_id(active_window);
	goto_mark;

	RM('USERIN^DB /PRE=LANG/NDF=1/F=MECONFIG/DPT=LANGUAGE.DB/LO=2/U=1/LT=LANGUAGE/DT=LANGUAGE SETUP/H=FE_LANGUAGE/GLO=@LANG/MACRO=SETUP^LANG_WARNING ' + mparm_str);
	set_global_int("@LANG",0);
}

macro PGM_MENU TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  PGM_MENU

Description:  Menu of compiler/program types for extension setup

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	RM('USERIN^DB /NDF=1/F=MECONFIG/DPT=PGM.DB/PRE=PT/GLO=!PGM/NE=1/NC=1/LO=1/U=1/LT=COMPILER////PROGRAM/H=FE_PROGRAM/MACRO=SETUP^PGM_WARNING');
}

macro PGM_DB TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  PGM_DB

Description:  Menu of program/compiler interfaces for the selected extension
							This macro is run by DATA_IN

Parameters:
							/PRE=   Prefix of the ISTR global.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

//  str  glob_name[20];
//  glob_name = parse_str('/PRE=', mparm_str ) + 'ISTR_1';
	RM('USERIN^DB /PRE=PGM/HPT=DEFAULT.PGM/NDF=1/F=MECONFIG/CBT=Done/DPT=' +
		parse_str("/EXT=",global_str("@T_EXT")) +
//      global_str( glob_name ) +
	'.PGM/LT=COMPILER////PROGRAM SETUP/NOALPHA=1/H=FE_PROGRAM');
}

macro PGM_WARNING TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  PGM_WARNING

Description:  Warns user that he cannot just arbitrarily add program types
							without having support for them.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	if(  (Parse_Int('/P=',MParm_Str))  ) {
		RM('MEERROR^MessageBox /T=WARNING/B=1/M=If you added any new program types, be sure that the macro CMPERROR in the macro file LANGUAGE.S has code to support it.');
	}
}

macro LANG_WARNING TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  LANG_WARNING

Description:  Warns user that he cannot just arbitrarily add language types
							without having support for them.  Also handles the
							reinitializing of syntax highlighting parameters on the fly.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str language[40];
	int active_window = window_id;
	mark_pos;

	if(  (Parse_Int('/P=',MParm_Str))  ) {
		language = parse_str("LANG=",global_str("@LANG"));
		if (length(global_str("@LANG")) >
			(svl(language) + 6)) {
			set_global_str("@" + copy(language,1,3) + "_SYNTAX_PARMS",global_str("@LANG"));
// this will reinit the syntax highliting for this language
			RM("SET_SYNTAX_PARMS /LANG=" + language);
		}
		if (parse_int("/NOCHECK=",mparm_str) == 0) {
//check record count against orignal amount
			RM("GET_DB_RECORD /NDF=1/F=MECONFIG/DPT=LANGUAGE.DB/DBF=LANG/AGLO=@TLANG/FV=" + language);
			if (global_int("@TLANG") > global_int("@LANG") )
				RM('MEERROR^MessageBox /T=WARNING/B=1/M=If you added any new language types, be sure that you have the appropriate support macros.');
			set_global_int("@LANG",0);
		}
	}
	switch_win_id(active_window);
	goto_mark;
}

macro EXT_FORMAT TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  EXT_FORMAT

Description:  Allows creation and/or editing of extension specific format lines.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int Temp_Integer,Jx;
	refresh = false;
	Temp_Integer = Cur_Window;
/* The format line editing macro "borrows" the line the cursor is on from the
current window.  We will make up a special window for this. */
	Switch_Window(Window_Count);
	Create_Window;
	jx = wherey + 1;
	if(  jx <= 0  ) {
		jx = 9;
	}
	Size_Window(1, jx,78,jx + 2);
	window_attr = 6;
	t_color = m_h_color;
	c_color = m_h_color;
	s_color = m_s_color;
	h_color = m_h_color;
	Put_Box(win_x1,win_y1,win_x2 + 2,win_y2 + 1,0,m_b_Color,'Editing format line',True);
	Format_Line = Global_Str('T_Format');
	if(  (Format_Line == '')  ) {
		Format_Line =
'                                                                      R                                          ';
	}
	Refresh = True;
	update_window;
	window_attr = 0;
	RM( 'MEUTIL1^EDFORMAT /EXTSET=1/M=1' );
	Set_Global_Str('T_Format',Format_Line);
	Refresh = False;
	Delete_Window;
	Switch_Window(Temp_Integer);
	Kill_Box;

/* This sets Using tab and margin setting to ON */
	Set_global_Int('EXTIINT_15',0);
	Set_global_Int('EXTIINT_16',1);
	Return_Str = 'Edit format line';
}

macro TGLVID TRANS2 {
/*******************************************************************************
															 MULTI-EDIT MACRO

Name:  TGLVID

Description:   /M=0     25 lines
									1     30/33 lines
									2     43/50 lines
									3     UltraVision support

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
		int Old_Mode;         /* The video mode before the toggle */

	Refresh = False;
	Old_Mode = Video_Mode;
	Set_Video_Mode( parse_int('/M=', mparm_str) );
	mouse = mouse;
	RM('SETSCRN');

	REFRESH = TRUE;
	NEW_SCREEN;
	Mou_Set_Limits( 1, 1, Screen_Width, Screen_Length );

}

#INCLUDE setcolor.s
#INCLUDE keycodes.s
#INCLUDE setsave.s