macro_file MEUTIL1;
/*******************************************************************************
												MULTI-EDIT MACRO FILE MEUTIL1

FILE_PROMPT - Standard file load/save prompt dialog box
LOADFILE		- Prompted file loading
SAVEFILE		- Prompted file saving
SAVEBLCK		- Prompted block saving
SPLICE			- Prompted block loading
FILESTAT		- Displays and allows changes to file name and lock status
EDFORMAT		- Edit the format line
EXEC				- Run programs or dos commands.  Controls memory and swapping.
REDIRECT		- Allows the macro EXEC to redirect in all screen modes and even
							STDERR.
SHELLDOS		- Run DOS from ME (uses EXEC).
RUNMAC			- Prompt to run a macro
REPEAT			- Repeat any single keystroke

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/


#define	_FPTAG		0x01
#define _FPTAGSAV 0x02
#define _FPTAGWIN	0x04

macro file_prompt trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: FILE_PROMPT

Description:	Creates a FILE dialog box, complete with directory window.

On Entry:			/T=str	Dialog box title
							/H=str	Help index
							/FTO=x
									0  	No filetype override
									1  	Show File Type Override radio buttons
									2   Same as above, but do NOT display the Default setting
							/FTC=x  Current file type
									0  	Default
									1  	MSDOS
									2  	Text
									3  	Binary

							/NHA=1  No History Add.  Use this parameter if you want
											additions to the file history list to be done outside
											of this prompt.

							/TAG=x	File tag type flag (bit fields)
									1		Enable file tagging.
									2 	Enable saving tag list to disk in TAGLST.TMP.
									4		Show tag list window.

							/NFT=1	No file type store in tag.

							RETURN_STR = default file name

On Exit:			RETURN_INT > 0 IF OK was pressed.  2 == Use TAGLST

							RETURN_STR = New file name.

							Sets global_int("!FILE_TYPE_OVERRIDE") to
									0 = use default filetype
									1 = use DOS type
									2 = use UNIX type
									3 = use Binary type

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int menu = menu_create,
			menu_count,
			tw = window_id,
			fto = parse_int("/FTO=", mparm_str),
			ft = parse_int("/FTC=", mparm_str),
			menu_level = global_int('MENU_LEVEL'),
			tag = parse_int("/TAG=", mparm_str),
			moff,
			b_col,
			dir_win,
			tag_win = 0;

	refresh = FALSE;
	if (Global_Str("!DEF_FILELOAD") != "") {
		return_str = Global_Str("!DEF_FILELOAD");
		rm('xlatecmdline /F=' + return_str);
	}
	if ((return_str == "") || (caps(return_str) == "?NO-FILE?")) {
		return_str = global_str( "FILE_HISTORY"
				+ parse_str("/#=", global_str("FILE_HISTORY")));
		if (return_str == "") {
			return_str = file_name;
		}
	}
	if (tag & _FPTAG) {
		switch_window(window_count);
		create_window;
		tag_win = window_id;
	}
	switch_window(window_count);
	create_window;
	dir_win = window_id;
	set_global_int("!FILE_TYPE_OVERRIDE", 0);

prompt_again:
	menu_set_item(menu, 1,
								"Name:", return_str,
								"/QK=1/C=1/W=54/ML=80/HISTORY=FILE_HISTORY/NHA="
										+ parse_str("/NHA=", mparm_str),
								0, 0, 0);

	if (tag & _FPTAG) {
		return_str = "/TAG=4";
	}
	else {
		return_str = "";
	}
	menu_set_item(menu, 2,
								"Files",
								"",
								"/QK=1/C=1/L=3/MSK=1/SDD=3" + return_str,
								20, 0, 0);

	switch_win_id(dir_win);
	menu_set_item(menu, 3,
								"Directories",
								"",
								"/OL=1/DC=1/QK=1/C=33/L=3/W=14/HT=14/WIN=" + str(cur_window),
								15, 0, 0);

	menu_count = 3;
	b_col = 50;
	moff = 0;
	if (tag & _FPTAG) {
		switch_win_id(tag_win);
		if (fto && !parse_int("/NFT=", mparm_str)) {
			return_str = "/TAGMAC=file_tag //MENU=" + str(menu) + "//FTO=" + str(fto);
		}
		else {
			return_str = "";
		}
		if (tag & _FPTAGWIN) {
			return_int = 15;
			b_col += 15;
		}
		else {
			return_int = 17;
		}
		menu_set_item(menu, ++menu_count,
									"Tag List",
									"",
									"/OL=1/QK=1/C=49/L=3/W=13/HT=14/WIN=" + str(cur_window)
											+ return_str,
									return_int, 0, 0);

		++moff;
	}
	menu_set_item(menu, ++menu_count,
								"OK",
								"",
								"/KC=<ENTER>/K1=13/K2=28/R=1/L=3/C=" + str(b_col),
								11, 0, 0);

	menu_set_item(menu, ++menu_count,
								"Cancel",
								"",
								"/KC=<ESC>/K1=27/K2=1/R=0/L=5/C=" + str(b_col),
								11, 0, 0);

	menu_set_item(menu, ++menu_count,
								"Help",
								"",
								"/KC=<F1>/K1=0/K2=59/R=2/QK=1/L=8/C=" + str(b_col),
								11, 0, 0);

	menu_set_item(menu, ++menu_count,
								"Sort",
								"DIRSHELL^DIRSORT /UPDATE=5",
								"/M=1/QK=1/L=10/C=" + str(b_col),
								11, 0, 0);

	if (inq_macro("fm_file_list") != - 1) {
		menu_set_item(menu, ++menu_count,
									"NameIt!",
									"",
									"/R=11/QK=5/L=12/C=" + str(b_col),
									11, 0, 0);
		++moff;
	}
	if(fto) {
		++menu_count;
		--b_col;
		if (fto == 2) {
			menu_set_item(menu, menu_count,
										"",
										"",
										"/L=14/C=" + str(b_col),
										10, 1, 0);
		}
		else {
			menu_set_item(menu, menu_count,
										"Default   ",
										"",
										"/QK=2/L=14/C=" + str(b_col),
										12, ft == 0, 0);
		}
		menu_set_item(menu, ++menu_count,
									"MSDOS text",
									"",
									"/QK=1/L=15/C=" + str(b_col),
									12, ft == 1, 0);

		menu_set_item(menu, ++menu_count,
									"Unix text ",
									"",
									"/QK=1/L=16/C=" + str(b_col),
									12, ft == 2, 0);

		menu_set_item(menu, ++menu_count,
									"Binary    ",
									"",
									"/QK=1/L=17/C=" + str(b_col),
									12,
									ft == 3, 0);
	}
	set_global_int("!MENU_OFF", moff);
	return_int = menu;
	rm("userIn^data_in /HN=1/T=" + parse_str("/T=", mparm_str)
			+ "/WM=" + parse_str("/WM=",mparm_str) + "/S=1/#=" + str(menu_count)
			+ "/H=" + parse_str("/H=", mparm_str)
// 			+ "/POSG=@FPPOS@/PRE=" + str(menu_level)
		);
	set_global_int("!MENU_OFF", 0);
	return_str = menu_item_str(menu, 1, 2);
	if (return_int == 11) {
		return_int = 0;
		rm("fileMngr^fm_file_list");
		if (return_int == 0) {
			goto prompt_again;
		}
	}
	if(return_int && fto) {

		int jx = 0;

		if (menu_item_int(menu, moff + 9, 2)) {
			jx = 1;
		}
		else if (menu_item_int(menu, moff + 10, 2)) {
			jx = 2;
		}
		else if (menu_item_int(menu, moff + 11, 2)) {
			jx = 3;
		}
		set_global_int("!FILE_TYPE_OVERRIDE", jx);
	}
	menu_delete(menu);
	if (switch_win_id(dir_win)) {
		delete_window;
	}
	if (switch_win_id(tag_win)) {

		str tstr;

		if (return_int || (tag & _FPTAG)) {
			tof;
			if ((tstr = get_line) != "") {
				do {
					put_line(str_del(tstr, 1, 13));
					down;
				} while ((tstr = get_line) != "");
				if (tag & _FPTAGSAV) {
					rm("tmp_file_name /FN=TAGLST.TMP");
					file_name = return_str;
					save_file;
				}
				if (return_int) {
					return_int = 2;
				}
			}
		}
		if (return_int != 2) {
			delete_window;
		}
	}
	if (return_int != 2) {
		switch_win_id(tw);
	}
}  // file_prompt

// MEUTIL1 new macro
macro file_tag trans2 {
/******************************************************************************
															 Multi-Edit Macro
															 13-Sep-93  17:49

	Macro   : file_tag
	Function: Macro called by file_prompt to create a tag list.
	Syntax  : Only to be called by file_prompt.

								 Copyright (C) 1993 by Small Systems Support.
**********************************************************************(ldh)***/

	int menu = parse_int("/MENU=", mparm_str),
			moff = global_int("!MENU_OFF"),
			jx = 0;

	return_str = "";
	if (parse_int("/FTO=", mparm_str)) {
		if (menu_item_int(menu, moff + 9, 2)) {
			jx = 1;
		}
		else if (menu_item_int(menu, moff + 10, 2)) {
			jx = 2;
		}
		else if (menu_item_int(menu, moff + 11, 2)) {
			jx = 3;
		}
		return_str = " /FTO=" + str(jx);
	}
}  // file_tag

// MEUTIL1 replacement macro
macro loadFile trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: loadFile

Description:  Prompts for and loads a single file into the current window.

Parameters:
							/BC=		Amount of boxes currently on screen
							/E=			If 1, indicates macro was called from Edit New File.
							/TAG=x	File tag type flags (bit fields)
									1		Enable file tagging.
									2 	Enable saving tag list to disk in TAGLST.TMP.
									4		Show tag list window.
							/NFT=1	No file type store in tag

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str tempstring,
			help_str[80];

	int jx,
			kill_count,
			edit_new_file = parse_int("/E=", mparm_str),
			tid = window_id,
			tag;

	if(xpos("/TAG=", mparm_str,1))
	{
		tag = parse_int("/TAG=", mparm_str);
	}
	else
	{
		tag = global_int("!FILE_TAG_MODE");
		if(!tag)
			tag = 1;
	}
	return_int = FALSE;
	kill_count = parse_int("/BC=",mparm_str);

	set_global_int("LOAD_COUNT", 0);

	tempstring = file_name;
	jx = 1;

	if (edit_new_file) {
		edit_new_file = 2;
		help_str = "/T=EDIT NEW FILE/H=EDITNEW";
	}
	else {
		help_str = "/T=LOAD FILE INTO CURRENT WINDOW/H=LOADFILE";
	}

	if ((!edit_new_file) && (caps(file_name) != "?NO-FILE?") && (link_stat == 0)) {
		rm('userIn^checkFile');
		jx = return_int;
	}
	return_int = FALSE;
	if (jx) {
		return_str = tempstring;
		rm("file_prompt /FTO=1/NHA=1/WM=1" + help_str
				+ "/TAG=" + str(tag)
				+ "/NFT=" + str(parse_int("/NFT=", mparm_str)));
		tempstring = return_str;
		if (return_int) {
			while (box_count > kill_count) {
				kill_box;
			}

			tag = 0;                     // Tmj
			if( return_int == 2 )        // Tmj
			{
				refresh = false;           // Tmj
				tag = window_id;           // Tmj
				switch_win_id( tid );      // Tmj
			}

			return_int = TRUE;
			error_level = 0;
			refresh = TRUE;
			return_str = tempstring;
			rm("ldFiles /DE=1/LC=0/CW=" + str(edit_new_file)
					+ "/FTO=" + str(global_int("!FILE_TYPE_OVERRIDE"))
					+ "/TAG=" + str(tag));
			set_global_int("!FILE_TYPE_OVERRIDE", 0);
			return_int = 1;
		}
	}
	else {
		return_int = FALSE;
	}
}  // loadFile


macro SAVEFILE TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: SAVEFILE

Description:  Prompts for and saves a single file from the current window.

Parameters:
							/NP=1			Bypass prompt, just save the file.
							/NM=1			No messages
							/NCO=1    No Confirm Overwrite
Returns:
							Return_Int - If 1, save was unsuccessful.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str TempString[128], old_ext[3];
	int Jx, kill_count, prompt_bypass, ft = 0,fto = 0;

	Return_Int = False;
	Kill_count = Parse_Int('/BC=',mparm_str);

	tempstring = File_Name;
	old_ext = caps(get_extension( tempstring ));
	prompt_bypass = parse_int('/NP=', mparm_str);
	if(  caps(file_name) == '?NO-FILE?'  ) {
		prompt_bypass = false;
	}


Try_Again:
	Return_Str = TempString;
	if(  prompt_bypass  ) {
		return_int = TRUE;
	} else {
		// Determine the file type
		if( line_terminator ==  '|13|10' )
		{
			ft = 1;
		}
		else if( line_terminator == '|10|0' )
		{
			ft = 2;
		}
		else if( copy(line_terminator,1,1) == '|0' )
		{
			ft = 3;
		}

		RM('FILE_PROMPT /FTO=2/NHA=1/T=SAVE FILE AS.../H=RENAMEFILE/WM=1/FTC=' + str(ft));

		// Check for a change in the file type
		// and convert if necessary
		fto = global_int("!FILE_TYPE_OVERRIDE");
		if(( fto != 0) && (fto != ft))
		{
			if(fto != ft)
			{
				if(!eof_in_mem)
				{
					working;
					mark_pos;
					eof;
					goto_mark;
				}
				ft = fto;
				switch ( ft )
				{
					case 1 :
						line_terminator = '|13|10';
						break;

					case 2 :
						line_terminator = '|10|0';
						break;

					case 3 :
						line_terminator = '|0|0';
				}
			}
		}
	}

	if(  Return_Int  ) {
		rm('setfilename');
		TempString = Return_Str;


		if((tempstring == caps(file_name)) && (!file_changed))
		{
			make_message("File not changed.");
			return_int = TRUE;
			goto exit;
		}

		if(  (First_Save) || (TempString != Caps(File_Name))   ) {
			if(!parse_int("/NCO=", mparm_str))
			if(  !global_int("NO_CONFIRM_OVERWRITE") || (TempString != Caps(File_Name)) ) {
				if(  File_Exists(TempString)  ) {
					RM('userin^VERIFY /H=SAVEFILE/T=' +
						TempString + ' exists;  Overwrite? ');
					if(  Return_Int == 0  ) {
						if(  prompt_bypass  ) {
							goto EXIT;
						}
						Goto Try_Again;
					}
				}
			}
			jx = file_changed;
			File_Name = TempString;
			file_changed = jx;
			if(  NOT(prompt_bypass)  ) {
				Return_Str = File_Name;
				RM('HISTORY_LIST /M=1/HISTORY=FILE_HISTORY');
			}
		}

		Return_Int = True;
 /*		while(  box_count > kill_count  ) {
			kill_box;
		}
 */
		if(  old_ext != caps(get_extension( file_name ))  ) {
			RM('USERIN^VERIFY /BL=FILENAME EXTENSION HAS CHANGED/T=Auto-setup for this extension?');
			if(  return_int  ) {
				RM('EXTSETUP');
			}
		}

		RM( 'FIND_EXT /EXT=.' + get_extension( file_name ));
		tempstring = parse_str( "SMAC=", global_str( return_str ) );

		if( svl(tempstring) != 0 )
		{
			return_int = TRUE;
			rm(tempstring);
			if( !return_int )
			{
				goto exit;
			}
		}
		if( !parse_int("/NM=", mparm_str) )
			Make_Message('Saving "'+File_Name+'"...');
		Save_File;

		Redraw;

		if( !parse_int("/NM=", mparm_str) )
		{
			if(  Error_Level == 0  ) {
				Make_Message('"'+File_Name+'" Saved.');
			} else {
				MAKE_MESSAGE('Error occured saving file "'+File_Name+'".');
			}
		}
		Return_Int = True;
	} else {
		Return_Int = False;
	}
EXIT:
}

macro SAVEBLCK TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: SAVEBLCK

Description:  This macro will save blocks as they are marked including column
							and stream marks. It will only copy the marked text.

Parameters:
							/BC=			Amount of boxes currently on screen
							/FN=			Default file name to save

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int result,orig_window,stat, kill_count,
			t_fixed_line_length = fixed_line_length;
	str blckname, t_line_terminator[2] = line_terminator;

	messages = false;

	Kill_count = Parse_Int('/BC=',mparm_str);

	if(  Block_Stat == 0  ) {
		make_message('No block marked.');
		Return_Int = False;
		Goto Exit;
	}

/*  PROMPT USER FOR BLOCK SAVE FILE NAME  */
	blckname = Parse_Str('/FN=',MParm_Str);

	if(  blckname == ''  ) {
Try_AGAIN:
		Return_Str = BlckName;
		RM('FILE_PROMPT /NHA=1/T=SAVE BLOCK AS.../H=SAVEBLOCK/WM=1');
		Result = Return_Int;
		if(  result  ) {
			rm('setfilename');
			RM('HISTORY_LIST /M=1/HISTORY=FILE_HISTORY');
		}
		BlckName = Return_Str;
	} else {
		result = true;
	}

		/* If escape was pressed in prompt then exit */
	if(  (result == 0)  ) {
		Goto Exit;
	}
	refresh = false;


	if(  File_Exists(Blckname)  ) {
		RM('userin^VERIFY /H=SAVEBLOCK/T='+ 'File exists;  Overwrite? ');
		if(  Return_Int == 0  ) {
			Goto Try_Again;
		}
	}
		/* If the this macro was called from the menu system then kill other menu boxes */
	while(  box_count > kill_count  ) {
		kill_box;
	}

/*  CREATE A NEW WINDOW - SAVE PLACE  */
	orig_window = Cur_Window;
	create_window;

/*  COULD WE CREATE THE WINDOW  */
	if(  (error_level)  ) {
		RM('MEERROR');
		Return_Int = True;
		goto exit;
	}

/*  COPY AND SAVE THE COPY  */
	file_name = blckname;
	line_terminator = t_line_terminator;
	fixed_line_length = t_fixed_line_length;
	window_copy(orig_window);
	save_file;

/*  COULD WE COPY AND SAVE  */
	if(  (error_level != 0)  ) {
		Make_Message('Error occured saving block.');
	} else {
		Make_Message('Block saved.');
	}
	RM('meerror');

	delete_window;
	switch_window(orig_window);
	Refresh = True;
	Redraw;
	return_int = true;
	error_level = 0;
Exit:
}

macro SPLICE TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: SPLICE

Description:  Loads("SPLICES") a file from disk into the current window without
							dumping the current file.  I.E. loads a file into a file.

							/BC=			Amount of boxes currently on screen

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str Temp_String, t_line_terminator[2];
	int Temp_Refresh,Active_Window,Temp_Window,kill_count;

	Temp_Refresh = Refresh;

	Return_Int = False;
	Kill_count = Parse_Int('/BC=',mparm_str);

QAGAIN:
	Temp_String = '';
	Return_Str = Temp_String;
	RM('FILE_PROMPT /NHA=1/T=MERGE FILE/H=LOADBLOCK/WM=1');
	Temp_String = Return_Str;
	if(  ((XPos('*',Temp_String,1) > 0) | (XPos('?',Temp_String,1) > 0))  ) {
		RM('MEERROR^MessageBox /B=2/T=ERROR/M=Wildcards are not allowed for this operation.');
		Goto QAGAIN;
	}
	if(  (Return_Int)  ) {
		Refresh = False;
		while(  box_count > kill_count  ) {
			kill_box;
		}
		Active_Window = Cur_Window;
		Create_Window;
		file_name = '!!SPLICE!!.TMP';
		Temp_Window = Cur_Window;
		Return_Int = 0;
		Error_Level = 0;
		return_str = temp_string;
		set_global_int('LOAD_COUNT', 0);
		RM('ldfiles /LC=0/MC=1/NW=1/NC=1/PRE=1/XL=1');
		Working;
		if(  error_level != 0  ) {
			RM('MEERROR^Beeps /C=1');
			Make_Message('File not found, or error loading.');
			Error_Level = 0;
		} else {
			Goto_Col(1);
			Block_Begin;
			Eof;
			Block_End;
			Switch_Window(Active_Window);
			Messages = False;
			Window_Copy(Temp_Window);
			Switch_Window(Temp_Window);
			Make_Message('"' + file_name + '" inserted into file.');
		}
		Delete_Window;
		RM('SetWindowNames');
		Switch_Window(Active_Window);
		Refresh = Temp_Refresh;
		return_int = 1;
	}

}

macro FILESTAT TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: FILESTAT

Description:  Allows the user to examine and change the current filename
and lock/readonly attribute.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  old_read_only, menu = menu_create ;
	int  jx,j1, j2, j3;
	str  old_ext[3],fts[20] ;

	j1 = first_file( file_name );
	if(  j1 == 0  ) {
		jx = last_file_time & $FFFF;
		j1 = jx >> 11;
		j2 = (jx >> 5) & $3F;
		return_str = Str_Del(Make_Time_Str( j1, j2, 0 ),6,3);

		jx = last_file_time >> 16;
		j1 = (jx >> 9) + 1980;
		j2 = (jx >> 5) & $0F;
		j3 = (jx & 31);
		return_str = 'Was last modified on ' + Make_Date_Str( j1, j2, j3 ) + ' ' + return_str;
	} else {
		return_str = 'Is a new file.';
	}

	// Determine the file type
	if( line_terminator ==  '|13|10' )
	{
		fts = 'MSDOS text';
	}
	else if( line_terminator == '|10|0' )
	{
		fts = 'UNIX text';
	}
	else if( copy(line_terminator,1,1) == '|0' )
	{
		fts = 'Binary';
	}

	menu_set_item( menu, 1, 'Name:', file_name ,
					'/QK=1/C=1/W=60/ML=80/L=1',0,0, 0);
	menu_set_item( menu, 2, 'File is:','',
					'/C=1/W=39/L=8',10,0, 0);
	menu_set_item( menu, 3, 'nOt locked and may be modified          ','',
					'/QK=2/C=2/L=9',12, (read_only == 0) , 0);
	menu_set_item( menu, 4, 'Locked/Readonly and may NOT be modified ','',
					'/QK=1 /C=2/L=10',12, read_only , 0);
	menu_set_item( menu, 5, 'Uses ' + str( cur_file_size ) +' bytes of disk space','',
					'/C=7/L=2',10,0, 0);
	menu_set_item( menu, 6, str( lines_in_mem ) +' lines of text are in memory','',
					'/C=7/L=3',10,0, 0);
	if ( eof_in_mem ) {
		menu_set_item( menu, 7, 'The entire file is in memory','',
					'/C=7/L=4',10,0, 0);
	}
	else
		menu_set_item( menu, 7, 'Not all of the file is in memory','',
					'/C=7/L=4',10,0, 0);
	menu_set_item( menu, 8, return_str,'',
					'/C=7/L=5',10,0, 0);

	menu_set_item( menu, 9, 'The file is ' + fts,'',
					'/C=7/L=6',10,0, 0);
	return_int = menu;
	RM('UserIn^Data_In /HN=1/H=FILEINFO/S=1/#=9/T=FILE INFORMATION');

	if(  return_int  ) {
		old_read_only = read_only;
		read_only = (menu_item_int( menu, 3, 2 ) == 0);
		if(  (read_only != old_read_only)  ) {
			RM('MEERROR^MessageBox /B=1/T=WARNING!/M=Please exercise caution when changing the LOCKED // READONLY attribute of a file.  ' +
							'If you are on a network, this action may have adverse effects on other users.');
		}
		cur_file_attr = (cur_file_attr & $FFFE) | (read_only != 0);
		if(  menu_item_str( menu, 1, 2 ) != file_name  ) {
			old_ext = get_extension( file_name );
			file_name = fexpand(menu_item_str( menu, 1, 2 ));
			if(  old_ext != caps(get_extension( file_name ))  ) {
				RM('USERIN^VERIFY /BL=FILENAME EXTENSION HAS CHANGED/T=Auto-setup for this extension?');
				if(  return_int  ) {
					RM('EXTSETUP');
				}
			}
		}
	}
	Return_Int = 100;
	menu_delete( menu );
}


macro EDFORMAT TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: EDFORMAT

Description:  This macro allows for editing of the format line for an
							individual file.

Parameters:	/M=1		Don't use message line....
							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int
					m = parse_int('/M=', mparm_str)
					;

	int TEMP_WRAP, Temp_TC, temp_changed ;

	TEMP_WRAP = WRAP_STAT;
	WRAP_STAT = FALSE;
	temp_tc = file_changed;

	Messages = False;
	UNDO_STAT = FALSE;

	Push_Labels;
	Flabel('Help',1,$FF);
	Mark_Pos;
	while(  C_Row > 1  ) {
		UP;
	}
	Set_Global_Str('!TEMP_LINE!', get_line );
	Temp_Changed = Line_Changed;
	Put_Line(Format_Line);
	update_window;
	if(  m  ) {
		Set_Global_Str('@EFEV#1', '/T=Don''t save/KC=<ESC>/K1=27/K2=1/R=0');
		Set_Global_Str('@EFEV#2', '/T=Save Changes/KC=<CR>/K1=13/K2=28/R=1');
		RM('CheckEvents /M=4/G=@EFEV#/#=2/X=' + str( win_x1 ) +
				'/Y=' + str(win_y2) + '/W=' + str(win_x2 - win_x1 + 1));
	} else {
		Make_Message('Editing format line.');
		Set_Global_Str('@EFEV#1', '/T=Don''t save/KC=<ESC>/K1=27/K2=1/R=0/W=15/X=40/Y='
		+ str(Message_Row));
		Set_Global_Str('@EFEV#2', '/T=Save Changes/KC=<CR>/K1=13/K2=28/R=1/W=16/X=23/Y='
		+ str(Message_Row));
	}
	RM('CheckEvents /M=2/G=@EFEV#/#=2');

EF_Loop:
	Read_Key;
	if(  Key1 == 13  ) {
ENTER:
		temp_tc = true;
		if(  m == 0  ) {
			Make_Message('Format line changes saved.  The format line WILL BE SAVED with the file.');
		}
		GOTO Save_Exit;
	}
	if(  Key1 == 27  ) {
ESCAPE:
		temp_tc=false;
		if(  m == 0  ) {
			Make_Message('Format line changes NOT saved. Old format line restored.');
		}
		GOTO Exit;
	}
	if(  Key1 == 9  ) {
		if(  (Length(Get_Line) < 999) & (C_Col < 999)  ) {
			Text('|16');
		} else {
			if(  m == 0  ) {
				Make_Message('Format line to long!');
			}
		}
		GOTO Ef_Loop;
	}
	if(  Key1 == 8  ) {
		if(  C_COL > 1  ) {
			Back_Space;
		}
		GOTO Ef_Loop;
	}
	if(  Key1 == 0  ) {
		if(  key2 == 250  ) {
			if(  (mou_last_x > win_x1) & (mou_last_x < win_x2) &
						(mou_last_y == wherey)  ) {
				RM('MOUSE^MouseInWindow');
				if(  return_int  ) {
					RM('MOUSE^Mouse_Move /H=1');
				}
			} else {
				RM('CheckEvents /M=1/G=@EFEV#/#=2');
				if(  (Return_Int == 1)  ) {
					Goto ESCAPE;
				} else if(  (Return_Int == 2)  ) {
					Goto ENTER;
				}
			}
		} else if(  Key2 == 77  ) {
			if(  C_Col < 999  ) {
				RIGHT;
			}
			GOTO Ef_Loop;
		}
		if(  key2 == 75  ) {
			if(  C_Col > 1  ) {
				LEFT;
			}
			GOTO EF_Loop;
		}
		if(  key2 == 83  ) {
			if(  NOT( At_Eol )  ) {
				Del_Char;
			}
			GOTO EF_Loop;
		}
		if(  key2 == 79  ) {
			EOL;
			GOTO EF_Loop;
		}
		if(  key2 == 71  ) {
			GOTO_COL(1);
			update_window;
			GOTO EF_Loop;
		}
		if(  key2 == 82  ) {
			Insert_Mode = NOT( Insert_Mode);
			GOTO EF_Loop;
		}
		if(  key2 == 116  ) {
			if(  C_Col < 999  ) {
				RIGHT;
				while(  (Cur_Char != '|0') & (C_Col < 999) & (Cur_Char != '|16')  ) {
					RIGHT;
				}
			}
			GOTO EF_Loop;
		}
		if(  key2 == 115  ) {
			if(  C_Col > 1  ) {
				LEFT;
				while(  (Cur_Char != '|0') & (C_Col > 1) & (Cur_Char != '|16')  ) {
					LEFT;
				}
			}
			GOTO EF_Loop;
		}
		if(  key2 == 59  ) {
							 Help('EDFMTLIN');
		}
		GOTO Ef_Loop;
	}

	if(  (Length(Get_Line) < 999) & (C_Col < 999)  ) {
		Text(Char(Key1));
	} else {
		if(  m == 0  ) {
			Make_Message('Format line to long!');
		}
	}

	GOTO EF_Loop;
Save_Exit:
	Format_Line = Get_Line;
	TEMP_WRAP = WRAP_STAT;
	Format_Stat = True;
Exit:

	Put_Line(global_str('!TEMP_LINE!'));
	Set_Global_Str('!TEMP_LINE!', '');
	RM('CheckEvents /M=3/G=@EFEV#/#=2');
	Line_Changed = Temp_Changed;
	file_changed = temp_tc;
	Goto_Mark;
	WRAP_STAT = TEMP_WRAP;
	UNDO_STAT = TRUE;
	Messages = True;
	Pop_Labels;
	update_window;
}

macro EXEC TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: EXEC

Description:  Executes a DOS command or program

Parameters:		Return_Str = the command line
							/MEM=nn	The amount of memory needed to run the program.
											(in paragraphs).
										0=ALL available.
							/SWAP=nn  The swap method to use
												0 = Use default method
												1 = No swap
												2 = Swap to disk only
												3 = Swap to additional memory only
												4 = Swap to additional memory then to disk
							/CMD=1		Use COMMAND.COM (or COMSPEC equivalent).
							/SCREEN=0	 Don't touch the screen.
											=1 Restore old dos screen.
											=2 Clear screen, put cursor at top
											=3 Redirect output to a screen window.
							IF /SCREEN=3:
											/X=n      x coordinate for window
											/Y=n      y coordinate for window
											/W=n      width of window
											/L=n      length of window
											/T=str    Title of window
											/WAIT=1   Wait for a key
											/NK=1     Don't kill box
											/CMDLN=1  Write the command line in the window.
							/RED=str  Redirect stdout to file str
							/REDERR=str Redirect stderr to file str.

									NOTE:  To redirect STDERR to the same file as STDOUT, simply specify
												 /REDERR=STDOUT

							/APPEND=1 Append redirect file instead of overwrite.

Returns:			The exit code of the DOS command or program will be in the
							integer EXIT_CODE.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int
					screen_mode,
					t_swap_mode,
					t_swap_mem,
					jx,
					t_status_row,
					x, y, w, l,
					tr,
					ohandle = -1,
					ohandle2 = -1,
					cmd = parse_int('/CMD=', mparm_str),
					wait = parse_int('/WAIT=', mparm_str)
				;
	str  cmd_line[128] = '',
			 prog[80] = return_str,
			 tstr[80]
					;

	set_global_int('MENU_LEVEL',global_int('MENU_LEVEL') + 1); //prevents autosave

	tr = refresh;
	refresh = false;
	t_swap_mem = swap_mem;
	t_swap_mode = swap_mode;
	t_status_row = status_row;
	status_row = 0;
	RM( 'XlateCmdLine' );
	jx = xpos(' ', return_str, 1 );
	if(  jx > 0  ) {
		cmd_line = copy(return_str, jx, 128 );
		prog = copy( return_str, 1, jx - 1 );
	} else {
		prog = return_str;
	}
	if(  CMD == 0  ) {
		if(  (get_extension( prog ) == '') | (get_extension(prog) == 'BAT')  ) {
			CMD = 1;
		} else if(  get_path(prog) == ''  ) {
			return_str = get_environment('PATH');
			RM('PATHSEARCH /F=' + prog );
			if(  return_int  ) {
				prog = return_str;
			} else {
				cmd = 1;
			}
		}
	}

	screen_mode = parse_int('/SCREEN=', mparm_str );
	swap_mem = parse_int('/MEM=', mparm_str);
	jx = parse_int( '/SWAP=', mparm_str );
	if(  jx > 0  ) {
		swap_mode = jx - 1;
	}
	if(  screen_mode == 1  ) {
		save_box(1,1,screen_width,screen_length);
		rest_dos_screen;
	} else if(  screen_mode == 2  ) {
		save_box(1,1,screen_width,screen_length);
		clear_screen( dos_color + (dos_back << 4 ));
		gotoxy(1,1);
	} else if(  screen_mode == 3  ) {
		Dos_Output_Window = 1;
		x = parse_int('/X=', mparm_str );
		y = parse_int('/Y=', mparm_str );
		l = parse_int('/L=', mparm_str );
		w = parse_int('/W=', mparm_str );
		if(  (l == 0) || (w == 0) || (x == 0) || (y == 0)  ) {
			x = (screen_width - 76) / 2;
			y = 6;
			l = 12;
			w = 76;
		}
		if(  (x + w) > screen_width  ) {
			w = screen_width - x;
		}
		if(  (y + l) > screen_length  ) {
			l = screen_length - l;
		}
		Put_Box( x, y, x + w + 3, y + l + 2, 0, m_b_color, parse_str('/T=', mparm_str), TRUE );
		dos_win_x1 = x + 1;
		dos_win_y1 = y + 1;
		dos_win_y2 = y + l;
		dos_win_x2 = x + w;
		gotoxy( dos_win_x1, dos_win_y1 );
		Set_Vp( dos_win_x1, dos_win_y1, dos_win_x2, dos_win_y2 );
		Text_Color_VP = m_t_color;
		if( parse_int( "/CMDLN=", mparm_str ) ) {
			write_vp( return_str + "\n\r" );
		}
	}

	tstr = parse_str('/RED=', mparm_str );
	if(( dos_output_window ) && (tstr ==  ""))
	{
		tstr = "NUL";
	}
	if(  tstr != ''  ) {
		if(  dos_output_window == 1  ) {
			dos_output_window = 2;
		}
		Return_int = 1;
		RM('REDIRECT /F=' + tstr +
				'/A=' + parse_str('/APPEND=', mparm_str) );
		ohandle = return_int;
	}
	tstr = parse_str('/REDERR=', mparm_str );
	if(( dos_output_window ) && (tstr ==  ""))
	{
		tstr = "NUL";
	}
	if(  tstr != ''  ) {
		if(  dos_output_window == 1  ) {
			dos_output_window = 2;
		}
		Return_int = 2;
		RM('REDIRECT /F=' + tstr +
				'/A=' + parse_str('/APPEND=', mparm_str) );
		ohandle2 = return_int;
	}
	shell_to_dos( prog + cmd_line, cmd );
	if(  ohandle > -1  ) {
		Return_Int = 1;
		RM('REDIRECT /M=1/SH=' + str(ohandle) );
	}
	if(  ohandle2 > -1  ) {
		Return_Int = 2;
		RM('REDIRECT /M=1/SH=' + str(ohandle2) );
	}
	switch ( screen_mode ) {
		case 0 :
			if ( Wait )
			{
				Make_Message ('* Press any key to continue *');
				Read_Key;
				Make_Message ('');
			}
			break;
		case 1 :
			if ( WAIT ) {
				write_Sod('* Press any key to continue *|13|10');
				Read_Key;
			}
			save_dos_screen;
			kill_box;
			break;
		case 2 :
			if ( WAIT ) {
				write_Sod('* Press any key to continue *|13|10');
				Read_Key;
			}
			kill_box;
			break;

		case 3 :
			if ( WAIT ) {
				write('* Press any key to continue *', x + (w / 2) - 12, dos_win_y2 + 1, 0, button_color );
				Read_Key;
			}
			Set_Vp( 1,1, screen_width, screen_length );
			dos_output_window = 0;
			if( !parse_int('/NK=', mparm_str) )
				kill_box;
			break;
	}
/*	if( (mou_installed) && (screen_width > 80) ) {
		set_global_int("~INIT_MOUSE", TRUE );
		rm("INIT_MOUSE");
	} */
	swap_mem = t_swap_mem;
	swap_mode = t_swap_mode;
	status_row = t_status_row;
	refresh = tr;
	Mou_Set_Limits( 1, 1, Screen_Width, Screen_Length );
	set_global_int('MENU_LEVEL',global_int('MENU_LEVEL') - 1); //prevents autosave
}

macro REDIRECT TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:  REDIRECT

Description: Allows the macro EXEC to redirect in all screen modes and even
							STDERR.

Parameters:
		 /M=0     Redirect handle to/from file or handle
				/D=n     0 = output
								 1 = input
				/A=1		 Append to file (instead of overwriting )
				/F=str   filename
				/H=handle   handle to redirect to.  Use this OR /F=, NOT both.
				RETURN_INT := handle to redirect;

				Returns:
					RETURN_INT = -1 if error
					RETURN_INT = handle to be saved (passed back to REDIRECT with /SH=n)

		/M=1      Restore handle
				RETURN_INT := handle
				/SH=saved handle

Standard handles:
						0       Standard input device           Stdin
						1       Standard output device          Stdout
						2       Standard error device           Stderr
						3       Standard auxiliary device       Stdaux
						4       Standard printer device         Stdprn

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  HANDLE, JX, result = -1, e, x ;
	str  fname[80] ;

	if(  parse_int('/M=', mparm_str) == 1  ) {
		r_AX = $4600;                        /*  Assign saved value back to Handle  */
		r_CX = return_int;
		jx = parse_int('/SH=', mparm_str);
		r_bx = jx;
		intr($21);
		if ( r_Flags & 01 )
			goto exit;

		if ( jx > 4 )
			e = s_close_file( jx );              /*  Close saved handle  */
	} else {
		fname = caps(parse_str('/F=', mparm_str ));
		e = 0;
		if ( "STDOUT" == fname )
			handle = 1;
		else if ( "STDERR" == fname )
			handle = 2;
		else if( "STDAUX" == fname )
			handle = 3;
		else if ( "STDPRN" == fname )
			handle = 4;
		else {
			if(  parse_int('/D=', mparm_str)  ) {
				e = s_open_file( fname, 0, handle );
			} else {
				if(  parse_int('/A=', mparm_str)  ) {
					e = s_open_file( fname, 1, handle );
					if(  e == 0  ) {
						e = s_move_file_ptr( handle, 2, 0 );
					}
				} else {
					e = s_create_file( fname, handle );
				}
			}
		}
		if(  e == 0  ) {
			r_ax = $4500;          /* duplicate file handle */
			r_bx = return_int;
			intr( $21 );
			x = r_ax;

			r_ax = $4600;          /* force duplicate handle */
			r_bx = handle;
			r_cx = return_int;
			intr( $21 );
			if(  (r_flags & 01) == 0  ) {  /* If no error then set result */
				result = x;
			} else {
				e = s_close_file( x );     /* If error then dump our dup handle */
			}
			if( handle > 2 )
				e = s_close_file( handle );
		}
	}

EXIT:
	return_int = result;
} /* REDIRECT */

macro SHELLDOS TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: SHELLDOS

Description:  Run DOS from EDIT mode, restoring the original DOS screen.

							You can modify the dos prompt by setting the global string
							DOS_PROMPT.  For example:

		Set_Global_Str("DOS_PROMPT", "(My prompt)" + Get_Environment("PROMPT"));

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str CMD_Parm[2], Batch_Parm[8], tprompt[255] = Get_Mstr_Env("PROMPT"),
			new_prompt[255] = Global_Str("DOS_PROMPT");
	int tr, t_environment_mode = environment_mode;

	tr = Refresh;
	Refresh = False;
	// Modify the prompt
	if( global_int("EXEC_ENV_MODE"))
	{
		environment_mode = 1;
		if( svl(new_prompt) != 0)
		{
			Set_Environment( "PROMPT", new_prompt );
		}
		else
		{
			Set_Environment( "PROMPT", "(Multi-Edit)" + tprompt );
		}
	}
	else
		environment_mode = 0;
	if ( '' == (Return_Str = Global_Str('@SHELL_TO_DOS_CMD')) ) {
		return_str = "<COMSPEC>";
	/*  TMJ - 12-31-92 11:54am
		R_AX = $D44D; R_BX = 0; INTR(0x2F);
		if ( R_AX == $44DD ) {
			CMD_Parm = ''; Batch_Parm = '__4DOS__';
		} else {
			CMD_Parm = '/C'; Batch_Parm = '';
		}
		return_str = '<COMSPEC> /E:1000 '+CMD_Parm+' <ME_PATH>DOSPROMPT '+Batch_Parm;
 */
	}
	rm('EXEC /SWAP=0/MEM=' + Str(Swap_Mem) + '/SCREEN=1');
	// Restore the old prompt
	if( environment_mode == 1 )
		Set_Environment("PROMPT", tprompt);
	Refresh = tr;
}

macro RUNMAC TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: RUNMAC

Description:  Prompt for and run a macro.

							/BC=			Amount of boxes currently on screen

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  kill_count ;

	Kill_count = Parse_Int('/BC=',mparm_str);

	set_global_str('!MAC_ISTR_1', global_str('MAC_RUN'));
	set_global_str('!MAC_IPARM_1', '/HISTORY=MACRO_HISTORY/C=1/W=40/T=Command line:/ML=254');
	rm('USERIN^Data_In /A=2/T=RUN MACRO/#=1/PRE=!MAC_/H=MC');
	Set_Global_Str('MAC_RUN',global_str('!MAC_ISTR_1'));
	Set_Global_Str('!MAC_ISTR_1', '');
	if(  (Return_Int)  ) {
		while(  box_count > kill_count  ) {
			kill_box;
		}
		RM(Global_Str('MAC_RUN'));
	}
	return_int = 100;
}

macro REPEAT TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:  Repeat

Description:  Performs a simple, single key command repeat function.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int
					k1,k2,          /*  Key codes to be repeated  */
					count,          /*  Number of repeats  */
					T_Refresh = Refresh
					;

	str
					count_str       /*  Stores the inputed string  */
					;

	redraw;
	if(  (Global_Int('@REPEAT_NO_REFRESH'))  ) {
		Refresh = False;
	} else {
		Refresh = True;
	}
	set_global_int("~IN_REPEAT",1);
	count = 0;
	count_str = '1';       /*  Start with a count of one  */
		/* Now read keys.  Build count_str until a non numeric key is pressed.
		 Use that key to repeat */
LP1:
	make_message('Repeat command ' + count_str + ' times;  Type command, count or <ESC>.');
	read_key;
	if(  ((key1 > 47) & (key1 < 58) & (key2 < 12))  ) {
		if(  count == 0  ) {
			count_str = CHAR(key1);
		} else {
			count_str = count_str + CHAR(key1);
		}
		++count;
		goto lp1;
	} else {
		make_message('');
		if(  (key1 == 27) & (key2 == 1)  ) {
			goto exit;
		}
		if(  val(count,count_str) == 0  ) {
			k1 = key1;
			k2 = key2;
			Push_Undo;
			if (count == 0) {
				while(  at_eof == 0  ) {
					Pass_key(k1,k2);
					while(  check_key  ) {    /* We must do this to avoid problems with */
						pass_key(key1,key2);    /*   nested keystroke macros  */
					}
				}
			} else {
				while(  count > 0  ) {
					Pass_key(k1,k2);
					while(  check_key  ) {    /* We must do this to avoid problems with */
						pass_key(key1,key2);    /*   nested keystroke macros  */
					}
					--count;
				}
			}
			Pop_Undo;
			redraw;
		}
	}
exit:
	set_global_int("~IN_REPEAT",0);
	Refresh = T_Refresh;
}
