macro_file modinst;
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	MODINST

Description: Multi-Edit Module Installation Macros

MODINST					- Multi-Edit Module Installation Menu
SETSOURCEDRIVE  - Prompts the user for the source drive/directory.
CHANGEDISK			- Prompts user to change disks.
INSTVERIFYDISK	- Checks to see if a disk is in the drive.
INSTALLBOXSTART	- A status message during installation of a module
INSTALLBOXDONE	- A status message after installation of a module
INSTALLFILES		- Copies files for selected module
INITINSTALL			- Main macro that gets run first time ME is installed.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

/****************** Multi-Edit INTEGER Macro Function ***********************

 NAME:         MCompileMacro

 DESCRIPTION:  Compiles the source specified by fn.  If the extension of fn
               is .HLP then it will compile a help file instead.

 RETURNS:      1 if successfuly, 0 if not

*****************************07-12-93 05:56pm*******************************/

#ifndef DEMO
int MCompileMacro( str fn )
        trans2
{
  int res = 1;
  int compile_mode, file_time = 0;

  if( get_extension( fn ) == "HLP")
  {
    compile_mode = 2;
    if( first_file( me_path + fn ) == 0 )
    {
      file_time = last_file_time;
    }
    return_str = ME_PATH + "UTILS\\HELPCX.EXE " + me_path + fn;
  }
  else
  {
    compile_mode = 1;
    return_str = ME_Path + "CMAC.EXE " + me_path + fn + " -P" + ME_Path + "MAC";
  }
  RM("EXEC /T=COMPILING/SCREEN=3/SWAP=4/NK=1");
  if(exit_code)
  {
    RM('MEERROR^MESSAGEBOX /T=ERROR/B=2/Y=20/M=Error ocurred compiling ' + fn );
    res = 0;
  }
  else
  {
    if ( compile_mode == 2 )
    {
      if( first_file( me_path + truncate_extension( fn ) + ".HLZ" ) == 0)
      {
        if( file_time <= last_file_time )
        {
          del_file( me_path + fn );
        }
      }
    }
  }
  kill_box;
  return(res);
}


void MInstalled( str m )
{
  RM('MEERROR^MESSAGEBOX /Y=3/M=' + m + ' has been installed.');
}
#endif // DEMO


macro MODINST {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: MODINST

Description: Module installation
 *
 * PARAMETERS: /UD=1    Use the INSTALL.LST from the floppy disk
 * 											instead of the default MEMISC.LST.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

#ifdef DEMO
	RM('meutil3^notinc /H=INMODINST/T=MODULE INSTALLATION' + mparm_str);
#else
  str  sstr[512], event_str[20], sfile[80],
			 title_str[80] = '/T=LANGUAGES // KEYMAPS // OTHER';
  int  twin = Window_Id, nwin = 0, ev_count, ud = 0;

  int menu = menu_create, bc = box_count;
	refresh = false;
	switch_window( window_count );
	error_level = 0;
	create_window;
	nwin = window_id;

/* Have the user enter the serial number */
	if(  (Global_Str('@ME_SERIAL#') == '')  ) {
		RM('SETUP^SETSERIAL');
	}

	if(  global_str('@SOURCE_DRIVE') == ''  ) {
		set_global_str( '@SOURCE_DRIVE', 'A:');
	}
	if(  global_str('@DESTINATION_DRIVE') == ''  ) {
		set_global_str( '@DESTINATION_DRIVE', me_path );
	}

	Return_Str = Global_Str('@SOURCE_DRIVE');
	if(  copy( return_str, length(return_str), 1 ) != '\'  ) {
		return_str = return_str + '\';
	}
	set_global_str('@SOURCE_DRIVE', return_str );

  if( first_file( me_path + 'UTILS\UNZIP.EXE' ) != 0 ) {
    rm('meerror^messagebox /B=2/T=UTILS\UNZIP.EXE IS MISSING/M=UNZIP.EXE (from the Program Disk) must be in ' + me_path + 'UTILS in order for Module Installation to work.');
		goto exit;
	}


load_again:
  if(  switch_win_id( nwin )  ) {
    if( file_changed )
      save_file;
    error_level = 0;
  }

	if( ud ) {
    title_str = "Add-on Module Installation";
		RM('InstVerifyDisk /F=INSTALL.LST/T=Add-on module');
		if( !return_int ) {
      ud = 0;
      goto load_again;
		}
		sstr = global_str('@SOURCE_DRIVE') + "INSTALL.LST";
		load_file( sstr );
		if( error_level ) {
			rm("MEERROR");
      error_level = 0;
      goto load_again;
		}
	}
	else {
    title_str = "Module Installation";
		sstr = me_path + 'UTILS\MEMISC.LST';
		load_file( sstr );
		if(  error_level != 0  ) {
				error_level = 0;
			sstr = global_str('@SOURCE_DRIVE') + 'MEMISC.LST';
			while( !file_exists( sstr ) ) {
        return_str = 'Module Installation';
				rm('ChangeDisk');
				if(  return_int == 0  ) {
          ud = 0;
          goto exit;
				}
			}
			if (copy_file( sstr, me_path + 'UTILS\MEMISC.LST', 0 ) != 0 ) {
				rm('MEERROR^MESSAGEBOX /T=ERROR COPYING FILE/M=A dos error occurred while copying MEMISC.LST to the Multi-Edit directory');
				goto exit;
			}
			goto load_again;
		}
	}
	RM('SETTABS /TS=8');


menu_again:
  while(box_count > bc)
    kill_box;

	goto_col(1);
	Ev_Count = 2;
	event_str =   Str(Global_Int( 'MENU_LEVEL' )) + 'MI';
  menu_set_item(menu, 1, 'Source drive/directory ',
    '','/L=2/C=37/QK=1/R=10', 11, 0, 0);
  menu_set_item(menu, 2, "ÀÄ\x1A " + global_str('@SOURCE_DRIVE'),
    '','/L=4/C=35', 10, 0, 0);
  menu_set_item(menu, 3, "³" ,
    '','/L=3/C=35', 10, 0, 0);
  menu_set_item(menu, 4, "ÚÄ" ,
    '','/L=2/C=35', 10, 0, 0);

  menu_set_item(menu, 5,'Install item(s)',
    '','/L=6/C=37/K1=13/K2=28/R=1/KC=<ENTER>',11,0,0);
  if(ud)
    menu_set_item(menu, 6,'Module installation',
      '','/L=8/C=37/R=11/QK=2',11,0,0);
  else
    menu_set_item(menu, 6,'Add-on installation',
      '','/L=8/C=37/R=11/QK=1',11,0,0);

  menu_set_item(menu, 7,'Done',
    '','/L=16/C=37/K1=27/K2=1/R=0/KC=<ESC>',11,0,0);
  menu_set_item(menu, 8,'Help',
    '','/L=16/C=50/K1=0/K2=59/R=2/KC=<F1>',11,0,0);
  menu_set_item(menu, 9,'Modules',
    '','/L=1/C=1/HT=16/DC=1/W=32/MRK=1/QK=1/WIN=' + str(cur_window),15,0,0);

  menu_set_item(menu, 10, "You may select multiple",'','/L=10/C=36', 10, 0, 0);
  menu_set_item(menu, 11, "items with the space-bar",'','/L=11/C=36', 10, 0, 0);
  menu_set_item(menu, 12, "or the right mouse button.",'','/L=12/C=36', 10, 0, 0);

  return_int = menu;
  ev_count = 12;
  RM('DATA_IN /H=INMODINST/NK=1/HN=1/S=9/A=2/X=' + parse_Str('/X=', mparm_str) + '/Y=' + parse_Str('/Y=', mparm_str) + '/S=2' +
    '/T=' + title_str + '/PRE=' + event_str + '/#=' + Str(Ev_Count)
			);

  if( return_int == 10 )
  {
    RM("SetSourceDrive");
    goto menu_again;
  }
  if(return_int == 11 )
  {
    ud = !ud;
    goto load_again;
  }
	if(  return_int == 1  ) {
		mark_pos;
		tof;
		reg_exp_stat = true;
    set_global_int("!INSTALL_NO_PROMPT", 0);
		if(search_fwd('%|175',0)) {
      set_global_int("!INSTALL_NO_PROMPT", 1);
			do {
				call install_item;
				eol;
			}
			while (search_fwd('%|175',0));
      set_global_int("!INSTALL_NO_PROMPT", 0);
		}
		else {
			goto_mark;
			call install_item;
		}
		goto menu_again;
	}
  if(parse_int("/V=", mparm_str))
  {
    RM('USERIN^Verify /T=Exit module installation?/L=4/C=6');
    if(  NOT(return_int)  ) {
      goto menu_again;
    }
  }
	goto exit;

install_item:
    if( !ud ) {
      while(  first_file( global_str('@SOURCE_DRIVE') + 'MEMISC.ZIP') != 0  ) {
        return_str = 'Languages / Keymaps / Other';
        rm('ChangeDisk');
        if(  return_int == 0  ) {
          ret;
        }
      }
    }

    goto_col(2);
		sstr = shorten_str(copy(get_word('|9'),1,31) );
		if(  (Xpos('KEYMAP',caps(Sstr),1))  ) {
			int menu = menu_create;
			menu_set_item( menu, 1,
				'This keymap will become the default keymap, changing the key assignments',
								'', '/C=1', 10, 0, 0);
			menu_set_item( menu, 2,
				'from those illustrated in the printed manual (on-line help will reflect',
								'', '/C=1', 10, 0, 0);
			menu_set_item( menu, 3,
				'these changes properly).   To return to the Multi-Edit keymap, after this',
								'', '/C=1', 10, 0, 0);
			menu_set_item( menu, 4,
				'installation, select KEY MAPPING from the INSTALLATION AND SETUP MENU.',
								'', '/C=1', 10, 0, 0);
			menu_set_item( menu, 5,
				'',
								'', '/C=1', 10, 0, 0);
			menu_set_item( menu, 6,
				'If the ' + sstr + ' is not desired, press <ESC> now!',
								'', '/C=1', 10, 0, 0);
			return_int = menu;
			RM('USERIN^DATA_IN /HN=1/PRE=INA/#=6/T=INSTALL ' + Caps(sstr));
			menu_delete( menu );
			if(  (Return_Int < 1)  ) {
				ret;
			}
		}

		goto_col(33);
		return_str = shorten_str( get_word( '/' ) );
		if(  return_str != ''  ) {
			if( !ud ) {
				return_str = 'MEMISC.ZIP ' + return_str;
				RM('InstallFiles "' + sstr + '"');
				if( !return_int ) {
					goto menu_again;
				}
			}
			else {
				sfile = parse_str("/F=", get_line );
				if( sfile == "" )
						sfile = "OPTION.ZIP";
				return_str = sfile + ' ' + return_str;
				RM('InstallFiles "' + sstr + '"');
				if( !return_int )
					goto menu_again;
			}
		}
		left;
    while(search_Fwd("/", 1) )
    {
      sstr = get_word("=");
      if(sstr == "/C")
      {
        right;
        sstr = shorten_str(get_word('/ '));
        MCompileMacro( sstr );
      }
      else
      if(sstr == "/R")
      {
        right;
        sstr = get_word('');
        RM( sstr );
      }
    }
		insert_mode = FALSE;
		goto_col(1);
		text('|251');
		insert_mode = TRUE;
	ret;

exit:
  while(box_count > bc)
    kill_box;
  menu_delete(menu);
	working;
	if(  switch_win_id( nwin )  ) {
		if( file_changed )
			save_file;
		delete_window;
	}
	switch_win_id( twin );
	return_int = 100;
  update_status_line;
#endif // DEMO
}


#ifndef DEMO

/*******************************************************************************
																MULTI-EDIT MACRO

Name: SetSourceDrive

Description:  Prompts the user for the drive/directory where the Multi-Edit
							disks will be located.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro SETSOURCEDRIVE TRANS {
	Create_Global_Str('!M_ISTR_1',global_str( '@SOURCE_DRIVE'));
	Create_Global_Str('!M_IPARM_1','/C=0/L=1/W=40/ML=80/H=/T=');
	RM( 'USERIN^DATA_IN /A=2/PRE=!M_/#=1/T=Input source drive//directory:/X=' + parse_str('/X=', mparm_str) + '/Y=' + parse_str('/Y=', mparm_str));
	if(  (Return_Int)  ) {
		Return_Str = fexpand(Global_Str('!M_ISTR_1'));
		if(  copy( return_str, length(return_str), 1 ) != '\'  ) {
			return_str = return_str + '\';
		}
		set_global_str( '@SOURCE_DRIVE', return_str );
	}
	Set_Global_Str('!M_ISTR_1','');
	return_int = 100;
}

macro CHANGEDISK TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: CHANGEDISK

Description: Prompts user to change disks.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int  x, y, jx ;

	x = 8;
	y = 5;
	put_box( x, y, x + 56, y + 12, 0, m_b_color, '> CHANGE DISK <', TRUE );

	Set_Global_Str('#CD!1',
				'/T=Ok/KC=<ENTER>/3D=1/W=9/K1=13/K2=28/R=1/Y=' + str(y + 9) +
										'/X=' + str( x + 15 ));
	Set_Global_Str('#CD!2',
				'/T=Cancel/KC=<ESC>/3D=1/W=11/K1=27/K2=1/R=0/Y=' + str(y + 9) +
										'/X=' + str( x + 29 ));
	jx = x + 28;
	Write( 'Please insert "' + return_str +'"' , jx - (length(return_str) / 2) - 9, y + 1, 0, m_s_color );
	Write( 'disk into drive ' + copy(global_str( '@SOURCE_DRIVE' ),1,2), jx - 9, y + 2, 0, m_s_color );
	Write( 'ÚÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ¿', jx - 14, y + 4, 0, cb_t_color );
	Write( '³    ÄÄÄÄÄÄÄÛÛÄÄÄÄÄÄÄ    ³', jx - 14, y + 5, 0, cb_t_color );
	Write( 'ÀÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÙ', jx - 14, y + 6, 0, cb_t_color );
	Write( 'Ü',jx - 14 + 26, y + 4, 0, button_shadow_color );
	Write( 'Û',jx - 14 + 26, y + 5, 0, button_shadow_color );
	Write( 'Û',jx - 14 + 26, y + 6, 0, button_shadow_color );
	Write( ' ßßßßßßßßßßßßßßßßßßßßßßßßßß', jx - 14, y + 7, 0, button_shadow_color );
	Write( '*', jx - 13, y + 5, 0, working_color );
	Write( Copy( global_str( '@SOURCE_DRIVE' ), 1, 2), jx - 12, y + 5, 0, cb_s_color );
	gotoxy(0,0);
	RM('CheckEvents /M=2/G=#CD!/#=2');
	update_status_line;
loop:
	read_key;
	if(  (key1 == 0) & (key2 == 250)  ) {
		RM('CheckEvents /M=1/G=#CD!/#=2');
	} else {
		RM('CheckEvents /M=0/G=#CD!/#=2');
	}
	if(  return_int > 0  ) {
		return_int = parse_int('/R=', return_str);
		goto EXIT;
	}
	GOTO LOOP;

EXIT:
	Kill_Box;
	RM('CheckEvents /M=3/G=#CD!/#=2');
	working;
}

macro InstVerifyDisk {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: INSTVERIFYDISK

Description: Checks to see if a disk is in the drive.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str  fname[80] ;
	fname = parse_str('/F=', mparm_str);
	working;
	while(  first_file( global_str('@SOURCE_DRIVE') + fname) != 0  ) {
		return_str = parse_str('/T=', mparm_str);
		rm('ChangeDisk');
		if(  return_int == 0  ) {
			goto EXIT;
		}
	}
	RETURN_INT = TRUE;
EXIT:
}

macro INSTALLBOXSTART TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: INSTALLBOXSTART

Description: A status message during installation of a module.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	put_box( 5, 7, 70, 15, 0, m_b_color, 'INSTALLING ' + return_str, TRUE );
	set_vp( 6, 8, 69, 13 );
	text_color_vp = m_s_color;

}

macro INSTALLBOXDONE TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: INSTALLBOXDONE

Description: A status message after installation of a module.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	if(  return_int  ) {
		RM('MEERROR^MESSAGEBOX /X=12/Y=12/M=' + return_str + ' has been installed.');
	}
}

macro INSTALLFILES TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: INSTALLFILES

Description: Copies files for selected module

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int bc = box_count,sl,j;
	str CurDir=FExpand(''),
			DstDir=Global_Str('@DESTINATION_DRIVE');

	// Truncate ending '\' if spec is not root dir of drive
	if ( ('\'==Copy(DstDir,sl=SVL(DstDir),1)) && (sl!=XPos('\',DstDIR,1)) )
		DstDir=Copy(DstDir,1,sl-1);

	// Truncate ending '\' if spec is not root dir of drive
	if ( ('\'==Copy(CurDir,sl=SVL(CurDir),1)) && (sl!=XPos('\',DstDIR,1)) )
		CurDir=Copy(CurDir,1,sl-1);

	Change_Dir(DstDir);
	if ( Error_Level )
	{
		str trs=Return_Str;

		Error_Level=0;
		rm('UserIn^Verify /T=Destination directory "'+DstDir+
				'" does not exist.  Do you wish to create it?');
		if ( Return_Int )
			if ( MKDir(DstDir) )
			{
				RM('MEERROR^MESSAGEBOX /B=2/Y=3/T=ERROR/M=Unable to create directory "'+
				DstDir+'"');
				Return_Int=0;
				Goto Abort;
			}
		Change_Dir(DstDir);
		Return_Str=trs;
	}
	while( j = xpos("\\", return_str,1) )
	{
		return_str = str_ins( "/", str_del(return_str,j,1), j);
	}

  return_str = me_path + 'UTILS\UNZIP.EXE -q -o ' + global_str('@SOURCE_DRIVE') + return_str;
	RM('MEUTIL1^EXEC /SCREEN=3/SWAP=4/RED=NUL/NK=1/T=' +
			'Installing ' + mparm_str
			);
	if(  error_level != 0  ) {
		rm('MEERROR');
		error_level = 0;
		return_int = 0;
	} else if ( exit_code != 0 ) {
    RM('MEERROR^MESSAGEBOX /B=2/Y=20/T=ERROR/M=An error occured while installing ' + mparm_str);
		return_int = 0;
	} else {
  //  if( !global_int("!INSTALL_NO_PROMPT") )
  //    RM('MEERROR^MESSAGEBOX /Y=3/M=' + mparm_str + ' has been installed.');
		return_int = 1;
	}

Abort:
	while( box_count > bc )
		kill_box;

	Change_Dir(CurDir);
}


macro INSTALL_KEYMAP {
// used by INSTALLOTHER to set globals for a keymap change.
	if ((xpos("KN=",mparm_str,1)) && (xpos("FN=",mparm_str,1))) {
		Set_Global_Int("SETUP_CHANGED",Global_Int("SETUP_CHANGED") | $02);
		Set_Global_Str("@KEYMAP_NAME@",mparm_str);
	}
}

macro INITINSTALL DUMP TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: INITINSTALL

Description: Main macro that gets run first time ME is installed.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int jx, do_convert = 0, twid = window_id;


	refresh = false;
	jx = 0;
	while(  jx < param_count  ) {
		++jx;
		return_str = parse_str('/SDRIVE=', param_str(jx));
		if(  return_str != ''  ) {
			return_str = fexpand( return_str );
			if(  copy( return_str, length(return_str), 1 ) != '\'  ) {
				return_str = return_str + '\';
			}
			set_global_str('@SOURCE_DRIVE', return_str );
		}
		if ( caps(param_str(jx)) == '/RUNCONVERT')
		{
			do_convert = 1;
		}
	}

	rm("INIT_MOUSE");

	if( do_convert )
	{
		create_window;
		load_file(me_path + 'DOC\CONV_700.DOC');
		error_level = 0;
		block_begin;
		search_fwd("\f",0);
		delete_block;
		read_only = true;
		tof;
		rm('EDITWINDOW /X=4/L=16/W=68/T=7.0 Updating and Conversion');
		delete_window;
		switch_win_id(  twid );
	}

  RM('ModInst /V=1');

	if( do_convert )
	{
		RM('CONV_700');
	}


	RM('Setup');
	return_str = me_path + 'DOC\README.DOC';
	RM('LdFiles');
}
#endif // DEMO