macro_file resedit;
/*******************************************************************************
														MULTI-EDIT MACRO FILE

Name: RESEDIT

Description: Routines for creating and editing menu resource files.

CREATE_RESOURCE	- Creates menu generating macros from a resource file.
LIST_RESOURCE		- Lists items in the resource file for editing.
R_EDIT_RESOURCE	- Provides a list for LIST_RESOURCE to facilitate editing.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

macro create_resource trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: CREATE_RESOURCE

Description: Creates menu generating macros from a resource file.

Parameters:
						/DBF=	Name of the resource file.  Defaults to RESOURCE.DB

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

    str   menu_name[80],
          dbf_name[80] = shorten_str(parse_str('/DBF=', mparm_str)),
					menu_parms[80] = '',
					tstr[80], tstr2[20],
					s1,s2,s3,s4, checked
          ;


    int   menu_count,
          owin = window_id,
          do_edit,
          top_menu,
					db_win, work_win,
          t,
          jx, jy, s,
					l1,l2,l3
          ;
		refresh = false;
		working;
		if (dbf_name == '')
				dbf_name = me_path + 'RESOURCE.DB';
		if ( get_path( dbf_name ) == "" )
		{
			dbf_name = me_path + dbf_name;
		}
		if(switch_file( caps(me_path + truncate_path(truncate_extension(dbf_name)) + '.S')))
				delete_window;
    refresh = FALSE;
    RM('SETCONFIG /DB=' + dbf_name + '/C=1');

    if( return_int ) {
			tof;
			db_win = cur_window;
			s1 = file_name;
			switch_window( window_count );
			rm('createwindow');
			file_name = me_path + truncate_extension( truncate_path( s1 ) ) + '.S';
			rm('EXTSETUP');
			make_message('Creating resource ' + file_name + ' from ' + dbf_name );
			put_line('// Resource created on ' + date + ' ' + time + ', version ' + version );
			down;
			put_line('// Do NOT modify this macro source.  All modifications should be done' );
			down;
			put_line('// through the resource file ' + truncate_path( dbf_name ) );
			down;
			put_line('macro_file ' + truncate_path( truncate_extension( file_name ) ) + ';');
			down;
			down;
			work_win = cur_window;
			switch_window( db_win );
			goto_col(1);
			while( !at_eof ) {
				if(search_fwd('%|12',0)) {
					right;
					menu_name = caps(get_word(' '));
					tstr = get_extension(menu_name);
					jx = xpos( '.', menu_name, 1 );
					if( jx != 0)
							menu_name = copy(menu_name, 1, jx - 1);
					if (tstr == 'MNU') {
						call process_menu;
					} else if (tstr == 'DLG') {
						call process_dlg;
					}
				} else
						eof;
			}

			switch_window( work_win );
			save_file;
			make_message('Done.');
			goto exit;


process_menu:
			menu_count = 0;
			right;
			menu_parms = get_word('');
			top_menu = parse_int('/HZ=', menu_parms);
			do_edit = (parse_int('/ED=', menu_parms) || parse_int('/ED=', mparm_str));
      reg_exp_stat = TRUE;
      down;
      goto_col(1);
			switch_window( work_win );
			return_str = '';
			if( parse_int('/TRANS=', menu_parms)) {
				return_str = ' trans2 ';
				var_remove_str( '/TRANS=', menu_parms );
			}
			put_line( 'macro _MN_' + copy(menu_name,1,12) + return_str + '  {');
			down;
			switch_window( db_win );
		loop:
			l1 = 0; l2 = 0; l3 = 0;
			s1 = ''; s2 = ''; s3 = '';
		  s2 = get_line;
			if( svl(s2) != 0) {
				if( str_char(s2,1) != '|12') {
        	++menu_count;
        	var_parse_int('|127TYPE=', s2, t);
        	if (t == 6 )
							l1 = 1;
					else
            	s1 = parse_str('|127NAME=', s2);
					var_remove_str( '|127NAME=', s2 );

					return_str = parse_str('|127HNDX=', s2 );
					var_remove_str( '|127HNDX=', s2 );
        	if(t == 5) {
							int tx;
            	if(!top_menu)
									s1 += '  |16';
							var_parse_str('|127CMD=', s2, tstr);
							tx = xpos( '^', tstr, 1 );
							if(tx != 0) {
								tstr2 = copy( tstr, 1, tx - 1 );
								tstr = copy( tstr, tx + 1, 254 );
							}
							else
									tstr2 = '';
           //   call conv_s2;
							s3 = '/S=5/H=' + return_str + '/ ' + s2 + '/M=MENU /MX=' + str(get_extension(tstr2) != '') +
									'/MN=' + tstr;
							if( tstr2 != '')
									s3 +=	'/DBF=' + tstr2;
        	} else {
            //  call conv_s2;
// mod to support the new Don't delete previous menu option 10-03-91 T.R.
              s3 = '/S=' + str(2 - parse_int("NMD=",s2)) + '/H=' +
								return_str + '/NM=1/ ' + s2;
        	}
					s2 = '';
        	down;
					switch_window( work_win );
					return_str = parse_str('|127OFFID=', s3 );
					if( copy( return_str, 1, 1 ) == '(' ) {
						var_remove_str('|127OFFID=', s3 );
					} else
						return_str = str(l1);
					checked = parse_str('|127CHECKED=', s3 );
					if( copy( checked, 1, 1 ) == '(' ) {
						var_remove_str('|127CHECKED=', s3 );
					} else
						checked = '0';
					put_line('  menu_set_item( return_int, ' + str(menu_count) + ', "' + s1 + '", ' +
																		 '"' + s2 + '", ' +
																		 '"' + s3 + '", ' +
																		 return_str + ', ' +
																		 checked
																		 + ',0);');
					down;
					switch_window( db_win );
					goto loop;
				}
      }
    }
    switch_window( work_win );
		put_line('  return_str = "/#=' + str(menu_count) + menu_parms + '";');
		down;
		put_line('}');
		down;
		down;
		switch_window( db_win );
	ret;

conv_s2:
  int cc;
  cc = 0;
  while ((cc = xpos( '/', s2, cc + 1 )) != 0) {
    s2 = str_ins( '//', str_del( s2, cc, 1 ), cc );
    ++cc;
  }
  ret;

process_dlg:
		right;
		menu_parms = get_word('');
    down;
    goto_col(1);
		switch_window( work_win );
		return_str = '';
		if( parse_int('/TRANS=', menu_parms)) {
			return_str = ' trans2 ';
			var_remove_str( '/TRANS=', menu_parms );
		}
		put_line( 'macro _DG_' + copy(menu_name,1,12) + return_str + '  {');
		down;
		switch_window( db_win );
		menu_count = 0;
    while( (cur_char != '|12') && !at_eof ) {
			s1 = ''; s2 = ''; s3 = ''; s4 = '';
      ++menu_count;
      s4 = get_line;
			l1 = 0; l2 = 0; l3 = 0;
			var_parse_str( 'NAME=', s4, s1 );
			var_parse_int( 'TYPE=', s4, l1 );
			var_parse_str('PARMS=', s4, s3 );
			s3 += "/L=" + parse_str('LINE=', s4 ) +
						"/C=" + parse_str('COL=', s4 ) +
						"/W=" + parse_str('WIDTH=', s4 );
    	switch_window( work_win );
			put_line('  menu_set_item( return_int, ' + str(menu_count )
													+ ', "' + s1
													+ '", "' + s2
													+ '", "' + s3
													+ '", ' + str(l1)
													+ ', ' + str(l2)
													+ ', ' + str(l3)
													+ ');');
      down;
			switch_window( db_win );
			down;
    }
    switch_window( work_win );
		put_line('  return_str = "/#=' + str(menu_count) + menu_parms + '";');
		down;
		put_line('}');
		down;
		down;
		switch_window( db_win );
	ret;

exit:
	 refresh = true;
	 redraw;
}

macro list_resource trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: LIST_RESOURCE

Description: Lists items in the resource file for editing.

Parameters:
						/F=	Name of resource file.  Defaults to RESOURCE.DB.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str resname[80] = parse_str('/F=', mparm_str),
			tstr;
	int work_win, reswin, menu, twin = window_id;

	refresh = false;

	if( resname == '')
			resname = 'RESOURCE.DB';

	if( get_path( resname ) == '')
			resname = me_path + resname;

	rm('SETCONFIG /DB=' + resname );
	if( return_int ) {
		tof;
		window_attr = 0;
		reswin = cur_window;
		rm('CREATEWINDOW');
		work_win = cur_window;
		switch_window( reswin );
		reg_exp_stat = TRUE;
		while( search_fwd( '%|12', 0 ) ) {
			right;
			tstr = get_word(' ');
			switch_window( work_win );
			put_line( tstr );
			down;
			switch_window( reswin );
		}
		switch_window( work_win );
		tof;
		window_attr = $FF;
		menu = menu_create;
		menu_set_item( menu, 1, resname, '', '/C=1/L=3/ATTR=' + str( m_s_color ), 10, 0, 0 );
		menu_set_item( menu, 2, 'Edit', 'R_EDIT_RESOURCE /DBF=' + resname, '/C=1/L=1/KC=<ENTER>/K1=13/K2=28/M=1/R=1', 11, 0, 0 );
		menu_set_item( menu, 3, 'New', 'R_NEW_RESOURCE /DBF=' + resname, '/C=14/L=1/KC=<INS>/K2=82/R=10/M=1', 11, 0, 0 );
		menu_set_item( menu, 4, 'Items in resource:', '', '/C=1/L=4/WIN=' + str( cur_window ), 15, 0, 0 );
		return_int = menu;
		rm('USERIN^DATA_IN /HN=1/T=RESOURCE EDITOR/CBT=Done/#=4/S=4/X=2');
		menu_delete( menu );
		switch_window( work_win );
		delete_window;
	}

	switch_win_id( twin );
}

macro r_new_resource trans2
{
	str tstr,resname = parse_str("/DBF=", mparm_str);
	int menu = menu_create, res_win = window_id;
	menu_set_item( menu,1, "Name: ", "", "/QK=1/C=1/L=1/W=20", 0,0,0);
	menu_set_item( menu,2, "Menu   ", "", "/C=2/L=3",12,1,0);
	menu_set_item( menu,3, "Dialog ", "", "/C=2/L=4",12,0,0);
	return_int = menu;
	rm('USERIN^DATA_IN /HN=1/T=CREATE NEW RESOURCE/#=3');
	if( return_int )
	{
		tstr = menu_item_str( menu, 1,2 );
		if ( menu_item_int( menu, 2, 2) )
		{
			tstr += ".MNU";
		}
		else
			tstr += ".DLG";
		rm("SETCONFIG /DB=" + resname + "/C=1/T=" + tstr );
		switch_win_id( res_win );
		eof;
		if(c_col > 1  )
			down;
		put_line( tstr );
		goto_col(1);
	}
	menu_delete(menu);
}

macro edit_user_dlg
{
/*******************************************************************************
																MULTI-EDIT MACRO

Name: EDIT_USER_DLG

Description: Brings up a scrolling list of a user definable menu.

Parameters:   /MN=str		menu name
							/ED=1			allow editing of sub-menus
							/DBF=str	filename to find menu

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
  str menu_name[20] = shorten_str(parse_str('/MN=', mparm_str));
  str dbf_name[80] = shorten_str(parse_str('/DBF=', mparm_str));
  int result, tx;

	refresh = false;
  if( menu_name == '') {
      menu_name = 'MAIN';
  }
	tx = xpos( '^', menu_name, 1 );
	if(tx != 0) {
		dbf_name = copy( menu_name, 1, tx - 1 );
		menu_name = copy( menu_name, tx + 1, 254 );
	}
	if (dbf_name == '')
			dbf_name = me_path + 'RESOURCE.DB';

	do {
		set_global_str('USERBUTTON1', '/T=Edit dialog parameters/QK=1/W=20/R=9');
  	RM('USERIN^DB /EBC=1/EBN=USERBUTTON/MOVE=1/ABT=Preview/NDF=1/F=' + dbf_name +
          	'/NOALPHA=1/HF=DIALOG.DB/HPT=DIALOG.HDR/DPT=' +
          	menu_name + '.DLG/LT=Edit dialog: ' + menu_name +
          	'/LO=2/H=USER/GLO=@USER@');
		set_global_str('USERBUTTON1', '');
  	result = return_int;
		if(  return_int == 1  ) {  // preview
			rm('preview_dlg /DB=' + dbf_name + '/MN=' + menu_name);
			return_int = true;
		} else if(return_int == 9) {
			rm('edit_dlg_parms /DB=' + dbf_name + '/MN=' + menu_name);
			return_int = TRUE;
		} else {
			return_int = FALSE;
		};
	} while( return_int );
  return_int = result;
}

macro edit_dlg_parms	trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: EDIT_DLG_PARMS

*******************************************************************************/
		int menu = menu_create;
		int jx,
				owin = window_id;
		str tstr;

    RM('SETCONFIG /DB=' + parse_str('/DB=', mparm_str) +
					'/C=1/T=' + parse_str('/MN=', mparm_str) + '.DLG');
		if(return_int) {
			return_int = menu;
			tstr  = get_line;
  		menu_set_item( return_int, 1, "Title: ", parse_str('/T=', tstr ), "/L=1/C=1/W=20/ML=80/QK=1", 0, 0, 0);
  		menu_set_item( return_int, 2, "Help index: ", parse_str('/H=', tstr), "/L=2/C=1/W=20/ML=40/QK=1", 0, 0, 0);
  			menu_set_item( return_int, 3, "Transient ", "", "/L=4/C=1/QK=2", 13, parse_int('/TRANS=', tstr), 0);
			jx = xpos( "/P=", tstr, 1 );
			if( jx > 0 )
			{
				tstr = copy(tstr,jx + 3, 2048);
			}
			else
				tstr = "";
  		menu_set_item( return_int, 4, "Other parameters: ", tstr, "/L=6/C=1/W=20/ML=80/QK=1", 0, 0, 0);
			return_int = menu;
			rm('USERIN^DATA_IN /HN=1/#=4/T=' + parse_str("/MN=", mparm_str) + ': DIALOG PARAMETERS' );
			if(return_int) {
				goto_col(1);
				forward_till(' ');
				if( menu_item_int( menu,3,2))
					text("/TRANS=1");
				put_line( copy(get_line,1, c_col - 1) +
									'/T=' + menu_item_str(menu,1,2) +
									'/H=' + menu_item_str(menu,2,2) +
									'/P=' + menu_item_str(menu,4,2)
									);
			}
		}
		menu_delete( menu );
		switch_win_id( owin );
}

macro preview_dlg	trans2 {
		int menu = menu_create;
		int jx, count = 0,
				owin = window_id;
		str tstr,tstr2,tstr3,tstr4[40];

    RM('SETCONFIG /DB=' + parse_str('/DB=', mparm_str) +
					'/C=1/T=' + parse_str('/MN=', mparm_str) + '.DLG');
		if(return_int) {
			return_int = menu;
			forward_till(" ");
			right;
			tstr2 = get_word("");
			down;
			goto_col(1);
			while( !at_eof && (cur_char != "\f"))
			{
				++count;
				tstr = get_line;
				tstr3 = "";
				if( (tstr4 = parse_str("LINE=", tstr)) != "")
				{
					tstr3 += "/L=" + tstr4;
				}
				if((tstr4 = parse_str("COL=", tstr)) != "")
				{
					tstr3 += "/C=" + tstr4;
				}
				if((tstr4 = parse_str("WIDTH=", tstr)) != "")
				{
					tstr3 += "/W=" + tstr4;
				}
				if((tstr4 = parse_str("HELP=", tstr)) != "")
				{
					tstr3 += "/H=" + tstr4;
				}
  			menu_set_item( menu, count,
					parse_str("NAME=", tstr),
					"",
					 tstr3 +
					 parse_str("PARMS=", tstr),
					parse_int("TYPE=", tstr), 0, 0);
				down;
			}
			return_int = menu;
			rm('USERIN^DATA_IN /HN=1/#=' + str(count) + tstr2 );
		}
		menu_delete( menu );
		switch_win_id( owin );
}



macro r_edit_resource trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: R_EDIT_RESOURCE

Description: Provides a list for LIST_RESOURCE to facilitate editing.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str ext[3] = get_extension( get_line );
	int twin = window_id;

	if( ext == 'MNU') {
		rm('EDIT_USER_MENU /MN=' + truncate_extension(get_line) + mparm_str );
	}
	else if( ext == 'DLG')
	{
		rm('EDIT_USER_DLG /MN=' + truncate_extension( get_line ) + mparm_str );
	}
	switch_win_id( twin );
	return_int = TRUE;
}
