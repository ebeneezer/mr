#define	PGS	1		// Modified 7.00p 1993/08/18 MLM
// PGS : Two outstanding bugs in NOTEBOOK resulted in loss of categories and
// PGS : incorrect deletions of category contents.  The first would overwrite
// PGS : the first char of the next category header, making it invisible and
// PGS : leading to mysterious (apparent) loss of contents by re-inserting a
// PGS : bogus empty category at the end of file.  The second, when an entire
// PGS : category was being deleted, expected category names to start in col.
// PGS : 2, *not* 4, where they really begin.  This resulted in all contents
// PGS : of the deleted category being left behind and occasional collisions
// PGS : on category names.
/****************************Multi-Edit Macro********************************

 NAME:         notebook

 DESCRIPTION:  A neat little notes organizer.  Allows you to specify any
							 number of categories and notes.  You can mark notes as "To Do"
							 and "Complete".  Is USER_ID specific for networks.

 PARAMETERS:
							/FN=	File name override.  Default name is NOTEBOOK.TXT.  If
 										Global_Str("~NOTEBOOK_NAME") is defined, it will use that.
										This parameter overrides both.


 RETURNS:

*****************************10-17-91 04:49pm*******************************/
macro notebook {

	int main_window = 0,
			category_window,
			note_list_window,
			edit_window,
			menu = menu_create,
			note_num,
			jx, jy, ll, cl,
			old_window = window_id;

	refresh = false;

	return_str = Global_Str("NOTEBOOK_NAME");

	if (parse_str("/FN=",Mparm_Str) != "") {
// If notebook file is specified by a macro parameter, use it
		return_str = fexpand(parse_str("/FN=",Mparm_Str));
	}


	switch_window( window_count );
	create_window;
	category_window = cur_window;
	create_window;
	note_list_window = cur_window;
	create_window;
	edit_window = cur_window;

	set_global_str('~NOTEBOOK_PARMS', '/EW=' + str( edit_window ) +
																 '/NW=' + str( note_list_window ) +
																 '/CW=' + str( category_window ) +
																 '/MW=' + str( main_window ) +
																 '/CAT#=5'
																 );
	rm("N_SET_NOTEBOOK");
//	cl = parse_int('/CL=', global_str('~NOTEBOOK_PARMS'));
//	ll = parse_int('/LL=', global_str('~NOTEBOOK_PARMS'));

	set_global_int( '~NOTEBOOK_MENU', menu );
	menu_set_item(menu, 1, 'Select', 'NOTES_SELECT', '/L=1/C=1/KC=<ENTER>/K1=13/K2=28/R=1/M=1', 11, 0, 0 );
	menu_set_item(menu, 2, 'Create', 'NOTES_CREATE', '/L=1/C=16/KC=<INS>/K2=82/R=10/M=1', 11, 0, 0 );
	menu_set_item(menu, 3, 'Delete', 'NOTES_DELETE', '/L=1/C=29/KC=<DEL>/K2=83/R=10/M=1', 11, 0, 0 );
	menu_set_item(menu, 4, 'Change Notebook', 'N_SET_NOTEBOOK /LIST=1', '/L=1/C=42/QK=2/R=10/M=1', 11, 0, 0 );
//	menu_set_item(menu, 4, '', '', '/L=1/C=44', 10, 0, 0 );
	menu_set_item(menu, 5, 'ûComplete', 'NOTES_SELECT /MARK=1', '/K2=60/L=1/C=64/KC=<F2>/R=9/M=1', 11, 0, 0 );
	menu_set_item(menu, 6, 'Categories:', '', '/DC=1/QK=1/L=3/C=1/W=22/HT=10/WIN=' + str( category_window ), 15, 0, 0 );
	menu_set_item(menu, 7, 'Notes:', '', '/DC=1/QK=1/L=3/C=25/W=52/HT=16/WIN=' + str( note_list_window ), 15, 0, 0 );
	menu_set_item(menu, 8, 'Modify Category', 'N_EDIT_CAT_NAME', '/L=14/C=1/KC=<F3>/K2=61/R=10/M=1', 11, 0, 0 );
	menu_set_item(menu, 9, 'Print', 'N_PRINT', '/L=16/C=1/KC=<F8>/K2=66/R=10/M=1', 11, 0, 0 );
	menu_set_item(menu, 10, 'Done', '', '/L=18/C=1/KC=<ESC>/K1=27/K2=1/R=0', 11, 0, 0 );
	menu_set_item(menu, 11, 'Help', '', '/L=18/C=12/KC=<F1>/K1=0/K2=59/R=2', 11, 0, 0 );


	return_int = menu;
	rm('USERIN^DATA_IN /H=NOTEBOOK^*/HN=1/#=11/S=7/T=NOTEBOOK/CBT=Done');
	goto exit;



exit:
	rm("n_close_notebook");
	menu_delete( menu );
	switch_window( edit_window );
	delete_window;
	switch_window( note_list_window );
	jx = c_line;
	delete_window;
	switch_window( category_window );
	set_global_str('~NOTEBOOK_PARMS', '/LL=' + str(jx) + '/CL=' + str(c_line) );
	delete_window;

	switch_win_id( old_window );

}

macro n_set_notebook
{
	str nstr[80] = global_str('~NOTEBOOK_PARMS');

	int main_window = parse_int('/MW=', nstr ),
			category_window = parse_int('/CW=', nstr ),
			note_list_window = parse_int('/NW=', nstr ),
			edit_window = parse_int('/EW=', nstr ),
			owin = cur_window,
			note_num,
			jx
			;

	return_int = 1;
	if( parse_int("/LIST=",mparm_str) == 1 ) {
		rm("n_close_notebook");
		return_str = "*.TXT";
		rm("MAKEUSERPATH");
		rm("FILE_PROMPT /T=SELECT NOTEBOOK/H=NOTEBOOK^*");
	}

	if( return_int ) {
		if( return_str == "" ) {
			return_str = 'NOTEBOOK.TXT';
			rm('MAKEUSERPATH');
		}

		error_level = 0;
		if( !switch_file( return_str ) ) {
			if( main_window == 0 ) {
				switch_window( window_count );
				create_window;
			}
			else
				switch_window( main_window );
      erase_window;
			main_window = cur_window;
			load_file( return_str );
			if( error_level != 0 ) {
				file_name = fexpand(return_str);
				error_level = 0;
				put_line( '0' );
				down;
				put_line( 'CATEGORIES' );
			}
			if (read_only) {
				RM('USERIN^VERIFY /H=NOTEBOOK^*/BL=NOTEBOOK FILE IS LOCKED/T=Unlock?/S=2');
				if( return_int ) {
					read_only = false;
    			cur_file_attr = (cur_file_attr & $FFFE);
				}
			}
		} else {
			main_window = cur_window;
		}
		set_global_str("NOTEBOOK_NAME", file_name );
		tof;
		jx = val( note_num, get_line );
		set_global_int( '~NOTEBOOK_NUM', note_num );

		set_global_str('~NOTEBOOK_PARMS', '/EW=' + str( edit_window ) +
																 	'/NW=' + str( note_list_window ) +
																 	'/CW=' + str( category_window ) +
																 	'/MW=' + str( main_window ) +
																 	'/CAT#=5'
																 	);



		reg_exp_stat = TRUE;
		rm( 'N_GET_CATEGORIES' );
		switch_window( category_window );
    tof;
    while( !at_eof && ( cur_char != "¯" ) )
      down;
    if(at_eof)
      tof;

		rm( 'n_get_list');
		switch_window( owin );
	}
}

macro n_close_notebook
{
	str nstr[80] = global_str('~NOTEBOOK_PARMS');

	int main_window = parse_int('/MW=', nstr ),
			category_window = parse_int('/CW=', nstr ),
			note_list_window = parse_int('/NW=', nstr ),
			edit_window = parse_int('/EW=', nstr ),
			owin = cur_window,
			note_num,
			jx,jy
			;

	reg_exp_stat = 1;
  switch_window( category_window );
  jx = c_line;
  switch_window( main_window );
  tof;
  search_fwd("%\f\f\fCATEGORIES", 0 );
  eol;
  if( search_fwd("%[¯\f]", 0 ) ) {
     if( (jx + 2) != c_line ) {
        insert_mode = FALSE;
				if( cur_char != "\f" )
        	text(" " );
        goto_line( jx + 2 );
        goto_col(1);
        text("¯");
        insert_mode = TRUE;
     }
  }

	switch_window(note_list_window);
	jx = c_line;
	jy = 0;
	switch_window( main_window );
	goto_line( global_int("~NOTEBOOK_LINE"));
	eol;
	if( search_fwd("%[¯\f]",0) ) {
		if( cur_char == "¯" ) {
			if( (c_line - global_int("~NOTEBOOK_LINE") ) == jx )
				goto no_change;
			insert_mode = FALSE;
			text(" ");
		}
	}
	goto_line( global_int("~NOTEBOOK_LINE") + jx );
	insert_mode = FALSE;
	goto_col(1);
#ifdef	PGS		// Don't trash next category if this one is empty!
	if(!At_Eof && (Cur_Char != "\f"))
	{	Text("¯");
	}
#else		// PGS
	text("¯");
#endif	// PGS
no_change:

	tof;
	jx = val( note_num, get_line );
	if (global_int('~NOTEBOOK_NUM') != note_num) {
		put_line( str(global_int( '~NOTEBOOK_NUM' )) );
	}

	if( file_changed )
			save_file;
	window_attr = $81;

	insert_mode = TRUE;
}

macro notes_select
{
		str nstr[80] = global_str('~NOTEBOOK_PARMS'),
				note_num[20],
				tstr[120];

		int main_window = parse_int('/MW=', nstr ),
				category_window = parse_int('/CW=', nstr ),
				note_list_window = parse_int('/NW=', nstr ),
				edit_window = parse_int('/EW=', nstr ),
				owin = cur_window,
				tpb = persistent_blocks,
				mark_complete = parse_int('/MARK=', mparm_str )
				;

		persistent_blocks = TRUE;
		if((owin == category_window) && !(mark_complete)) {
			rm( 'n_get_list');
		} else if ((owin == note_list_window) || mark_complete) {
			int jx, jy;
			jy = c_line;
			jx = parse_int('|127NUM=', get_line );
			switch_window( main_window );
			goto_line(global_int('~NOTEBOOK_LINE'));
			reg_exp_stat = TRUE;
			if( search_fwd('%|12NUM=' + str(jx) + '|12', 0)) {
				jx = c_line;
				return_str = get_line;
				note_num = parse_str('|12NUM=', return_str);
				down;
				block_begin;
				goto_col(1);
				if( search_fwd('%|12', 0)) {
					if (c_line == block_line1) {
						block_off;
					} else {
						up;
						block_end;
					}
				} else {
					if(at_eof)
							block_off;
					else {
						eof;
						block_end;
					}
				}
				switch_window( edit_window );
				erase_window;
				window_copy( main_window );
				persistent_blocks = tpb;
				if( mark_complete ) {
					tstr = return_str;
					var_remove_str('DONE=', tstr );
					return_str = tstr + 'DONE=1';
					return_int = TRUE;
				}
				else
					rm('n_edit_note /EDIT=1');
				persistent_blocks = TRUE;
				if( return_int ) {
					int to_do = parse_int('|127DO=', return_str ) && !parse_int('|127DONE=', return_str);
					tof;
					block_begin;
					eof;
					block_end;
					switch_window( main_window );
					goto_line( jx );
					delete_block;
					tstr = return_str;
					var_remove_str( '|127MD=', tstr );
					return_str = tstr + '|127MD=' + date + ' ' + str_del( time, 6, 3 );
					if ( !mark_complete )   /* DKF */
						return_str = '|12NUM=' + note_num + '|12' + return_str;
					put_line( return_str );
					down;
					window_copy( edit_window );
					block_off;
					goto_line( global_int('~NOTEBOOK_LINE') );
					jx = 0;
					while( jx++ < jy )
							down;
					del_line;
					rm("sort_note");
					jx = return_int;
					RM('n_list_entry /N=' + note_num );
					put_line( return_str );
					switch_window( note_list_window );
					goto_line( jy );
					del_line;
					goto_line( jx );
					goto_col(1);
					if(!at_eof) {
            jx = line_attr;
						cr;
            line_changed = FALSE;
            line_attr = jx;
						up;
					}
					put_line( return_str );
					line_changed = FALSE;
					if( to_do ) {
						line_attr = cb_s_color;
						rm('n_set_cat /ON=1');
					} else {
						line_attr = 0;
						reg_exp_stat = TRUE;
						mark_pos;
						tof;
						to_do = search_fwd('%?@*', 0);
						goto_mark;
						rm('n_set_cat /ON=' + str(to_do) );
					}
				}
			}
		}
		switch_window( owin );
		persistent_blocks = tpb;
}

macro sort_note
{
	int jx;
	str ds[8] = parse_str("CD=", return_str),
			ys[2] = copy(ds,7,2),
			tstr[128];

	goto_line( global_int('~NOTEBOOK_LINE') );
	down;
	goto_col(1);

	ds = copy(ds,1,5);
	jx = 1;
	tstr = get_line;
	if( parse_int('DO=', return_str) && !parse_int('DONE=', return_str) ) {
		while( !at_eof && (copy(tstr, 1, 1) != "\f")
					&& (copy(tstr,2,1) == "*")
					&& (copy(tstr,50,2) >= ys)
					&& (copy(tstr,44,5) > ds)) {
					++jx;
					down;
					tstr = get_line;
		}

	}
	else {
		while( !at_eof && (copy(tstr, 1, 1) != "\f")
					&& (((copy(tstr,50,2) >= ys)
					&& (copy(tstr,44,5) > ds)) || (copy(tstr,2,1) == "*"))
					) {
					++jx;
					down;
					tstr = get_line;
		}
	}
	if(!at_eof) {
		cr;
		up;
	}
	return_int = jx;
}

macro notes_delete
{
		str nstr[80] = global_str('~NOTEBOOK_PARMS');

		int main_window = parse_int('/MW=', nstr ),
				category_window = parse_int('/CW=', nstr ),
				note_list_window = parse_int('/NW=', nstr ),
				edit_window = parse_int('/EW=', nstr ),
				owin = cur_window,
				tpb = persistent_blocks
				;
		int jx, jy;

		persistent_blocks = TRUE;

		if(owin == category_window) {
#ifdef	PGS	// Fix offset (4, not 2) so delete works correctly
			Rm("UserIn^Verify /H=NOTEBOOK^CATEGORIES%DELETE/T=All notes in \"" +
					Shorten_Str(Copy(Get_Line, 4, 20)) + "\" will be lost!");
#else		// PGS
			RM('USERIN^VERIFY /H=NOTEBOOK^CATEGORIES%DELETE/T=All notes in "' + shorten_str( copy(get_line,2,20))
									 + '" will be lost!');
#endif	// PGS
			if( return_int ) {
				jx = c_line;
#ifdef	PGS	// Fix offset (4, not 2) so delete works correctly
				Return_Str = Shorten_Str(Copy(Get_Line, 4, 20));
#else		// PGS
				return_str = shorten_str(copy( get_line, 2, 20 ));
#endif	// PGS
				switch_window( main_window );
				tof;
				down;
				jy = 0;
				while( jy < jx ) {
					down;
					++jy;
				}
				del_line;
				goto_col(1);
				reg_exp_stat = TRUE;
				if( search_fwd( '%|12|12' + return_str + '~~~!', 0 )) {
					block_begin;
					eol;
					if( !search_fwd('%|12|12?*@~@~@~!',0))
							eof;
					else
							up;
					block_end;
					delete_block;
				}
				switch_window( category_window );
				del_line;
				goto_col(1);
				if(block_stat) {
					goto_line(block_line1);
				} else if (at_eof) {
					up;
				}
				rm('n_get_list');
			}
		} else if (owin == note_list_window) {
			RM('USERIN^VERIFY /H=NOTEBOOK^NOTES%DELETE/T=Delete this note?');
			if( return_int ) {
				jy = c_line;
				jx = parse_int('|127NUM=', get_line);
				del_line;
				switch_window( main_window );
				goto_line(global_int('~NOTEBOOK_LINE'));
				reg_exp_stat = TRUE;
				if( search_fwd('%|12NUM=' + str(jx) + '|12', 0)) {
					jx = c_line;
					block_begin;
					eol;
					if( search_fwd('%|12', 0)) {
						up;
						block_end;
					} else {
						eof;
						block_end;
					}
					delete_block;
					goto_line( global_int('~NOTEBOOK_LINE') );
					jx = 0;
					while( jx++ < jy )
						down;
					del_line;
				}
			}
		}
		switch_window( owin );
		persistent_blocks = tpb;
}


macro n_list_entry
{
	str tstr[120];

	tstr = parse_str('|127SBJ=', return_str );
	pad_str( tstr, 40, ' ' );
	tstr = tstr + parse_str('|127CD=', return_str);
	if( parse_int('|127DONE=', return_str) )
			tstr = ' û ' + tstr;
	else if( parse_int('|127DO=', return_str) )
			tstr = ' * ' + tstr;
	else
			tstr = '   ' + tstr;
	pad_str( tstr, 59, ' ' );
	tstr += '|127NUM=' + parse_str('/N=', mparm_str);
	return_str = tstr;
}

macro n_print trans {
	str nstr[80] = global_str('~NOTEBOOK_PARMS');

	int   main_window = parse_int('/MW=', nstr ),
				note_list_window = parse_int('/NW=', nstr),
				category_window = parse_int('/CW=', nstr ),
				owin = cur_window,
				to_do_only = 0, summary = 1,
				handle = 0,
				jx, line_count, page_count = 1
				;
	str header_str[128], tstr, margin_str[80];

	int menu = menu_create
			;

MENU_AGAIN:
	return_str = global_str('PRINTER_TYPE');
	menu_set_item( menu, 1, 'print Category ','',	'/L=1/C=1/QK=7/R=10',11,0, 0);
	menu_set_item( menu, 2, 'print single Note','',	'/L=3/C=1/QK=14/R=11',11,0, 0);
	menu_set_item( menu, 3, 'eject Page ', 'PRN_EJECT_PAGE', '/L=5/C=1/QK=7/M=1/R=12',11,0, 0);
	menu_set_item( menu, 4, 'Setup printer', 'PRN_CODES',	'/L=7/C=1/QK=1/M=1/R=12',11,0, 0);
	menu_set_item( menu, 5, 'printer Type       ', return_str,'/L=1/W=20/C=25/QK=9/M=PRN_TYPE',8,0, 0);
	menu_set_item( menu, 6, 'printer Device/file', global_str('PRINTER_DEVICE'),	'/L=2/W=20/C=25/QK=9/M=PRN_SET_DEVICE',8,0, 0);
	menu_set_item( menu, 7, 'print Margin       ','',	'/L=3/W=3/C=25/QK=7',1, print_margin, 0);
	menu_set_item( menu, 8, 'print sUmmary      ','',	'/L=5/C=25/QK=8',13, summary, 0);
	menu_set_item( menu, 9, '"To Do" items only ','',	'/L=6/C=25/QK=5',13, to_do_only, 0);

	return_int = menu;
	RM('USERIN^Data_In /HN=1/POSG=@NPRPOS@/PRE=NPRD/A=1/S=1/#=9/T=NOTEBOOK PRINT/H=NOTEBOOK^PRINT/ABT=Done');

	if (return_int) {
		summary = menu_item_int( menu, 8, 2 );
		to_do_only = menu_item_int( menu, 9, 2 );
		print_margin = menu_item_int( menu, 7, 2 );
		if ((return_int == 10) || (return_int == 11)) {
			jx = return_int;
			RM('PRN_CHECK_DEVICE');
			if ( return_int ) {
				Handle = global_int('PRINTER_HANDLE');
				if( jx == 10 ) {
					call print_category;
				}
				else if( jx == 11 ) {
					call print_note;
				}

				RM('PRINT_CODE_MENU /T=1');
				if(  (Return_Str != '')  ) {
					RM('PRINTSTR /S=1/H=' + Str(Handle));
					if(  (Error_Level)  ) {
						RM('Meerror');
					}
				}
			}
		}
	}
	if(  (Handle)  ) {
		RM('OPEN_CLOSE_FILE /H=' + Str(Handle));
		set_global_int('PRINTER_HANDLE', 0);
	}
	goto exit;

print_note:
	call setup_print;
	switch_window( note_list_window );
	return_str = parse_str("NUM=", get_line);
	switch_window( main_window );
	goto_line( global_int('~NOTEBOOK_LINE') );
	if( search_fwd('|12NUM=' + make_literal(return_str) + '|12',0)) {
		call print_note_line;
		down;
		goto_col(1);
		while( !at_eof && (cur_char != '|12')) {
			tstr = '    ' + get_line;
			call print_line;
			down;
		}
	}
	ret;


setup_print:
	reg_exp_stat = TRUE;
	switch_window( main_window );
	goto_line( global_int('~NOTEBOOK_LINE') );
	pad_str( margin_str, print_margin, ' ');
	return_str = copy(get_line,3,40);
	jx = xpos('~~~!', return_str,1);
	if( jx != 0 ) {
		return_str = copy(return_str,1, jx - 1 );
	}
	Header_Str = margin_str + "Category:  " + return_str;
	pad_str(header_str, 40, ' ' );
	header_str += date + ' ' + str_del(time,6,3);
	pad_str(header_str, 80 - print_margin - 5, ' ' );
	header_str += 'Pg. ';
	line_count = 0;
	ret;


print_category:
	call setup_print;
	return_str = header_str + str( page_count ) + '|13|10|10';
	rm('printstr /H=' + str(handle));

	if( summary ) {
		tstr = "Summary:";
		call print_line;
		down;
		goto_col(1);
		while( cur_char != '|12' ) {
			if( !to_do_only || (copy(get_line,2,1) == '*')) {
				tstr = '    ' + copy(get_line,1,60);
				call print_line;
			}
			down;
		}
	}
	else {
		eol;
		if (!search_fwd('%|12', 0 ))
				goto pdone;
	}

	while( (copy(get_line,1,2) != '|12|12') && !at_eof) {

		if( cur_char == '|12') {		// if this is a header line...
			switch ( to_do_only )
			{
				case  1:
					if ( (parse_int('DO=',get_line) && !(parse_int('DONE=',get_line))) )
					{
						call print_note_line;
					}
					else
					{
						eol;
						if ( !search_fwd('|12',0) )		// skip ahead to next header
						{
							eof;
						}
						up;	// adjust for down stmt below
					}
					break;
				case  0:
					call print_note_line;
					break;
			}
		}
		else {
			tstr = '    ' + get_line;
			call print_line;
		}
		down;
	}
pdone:
	ret;


print_line:
		++line_count;
		if( line_count > 54 ) {
			call form_feed;
		}
		return_str = margin_str + tstr + '|13|10';
		rm('printstr /H=' + str(handle));
	ret;


print_note_line:
	tstr = '';
	call print_line;
	if( line_count > 49 ) {
		call form_feed;
	}
	pad_str( tstr, 75 - print_margin, '-');
	call print_line;
	tstr = 'SUBJECT:  ' + parse_str('SBJ=', get_line );
	call print_line;
	tstr = 'CREATED: ' + parse_str('CD=', get_line )
				+ '  MODIFIED: '  + parse_str('MD=', get_line );
	if( parse_int('DONE=', get_line) )
			tstr += ' >>> DONE';
	else if( parse_int('DO=', get_line) )
			tstr += ' >>> TO DO';
	call print_line;
	tstr = '';
	call print_line;
	ret;

form_feed:
	++page_count;
	line_count = 0;
	return_str = '|12' + header_str + str( page_count ) + '|13|10|10|10';
	rm('printstr /H=' + str(handle));
	ret;

exit:
	return_int = 100;
	menu_delete( menu );
	switch_win_id( owin );
}

macro n_edit_cat_name
{
		str nstr[80] = global_str('~NOTEBOOK_PARMS');

		int main_window = parse_int('/MW=', nstr ),
				category_window = parse_int('/CW=', nstr ),
				owin = cur_window
				;
		int jx, jy;
		str tstr[10];

		switch_window( category_window );
		jx = c_line;
		nstr = copy(get_line, 4, 20);
		tstr = copy( get_line, 1, 3 );
		return_str = nstr;
		rm('USERIN^QUERYBOX /H=NOTEBOOK^CATEGORIES%Modify Category/T=NEW NAME OF CATEGORY/W=20');
		if( return_int ) {
			return_str = shorten_str(return_str);
			put_line('');
			mark_pos;
			tof;
			if(search_fwd('% ' + return_str + '$',0)) {
				RM('MEERROR^MESSAGEBOX /M=Category already exists!');
				goto_mark;
				put_line( '  ' + nstr );
				goto exit;
			}
			goto_mark;
			put_line(tstr + return_str );
			switch_window( main_window );
			tof;
			goto_line( jx + 2 );
			put_line(tstr + return_str );
			reg_exp_stat = TRUE;
			if( search_fwd( '%|12|12' + nstr + '~~~!', 0 ) ) {
				put_line('|12|12' + return_str + '~~~!' );
			}
		}
	exit:
		switch_window( owin );
}

macro n_set_cat {
		str nstr[80] = global_str('~NOTEBOOK_PARMS');

		int main_window = parse_int('/MW=', nstr ),
				category_window = parse_int('/CW=', nstr ),
				owin = cur_window,
				ti = insert_mode;
				;
		int jx, jy;

		switch_window( category_window );
		mark_pos;
		jx = c_line;
		goto_col( 2 );
		insert_mode = false;
		if( parse_int('/ON=', mparm_str )) {
				text('*');
				line_attr = cb_s_color;
		} else {
				text(' ');
				line_attr = 0;
		}
		line_changed = false;
		goto_mark;
		switch_window( main_window );
		mark_pos;
		tof;
		goto_line( jx + 2 );
		goto_col( 2 );
		if( parse_int('/ON=', mparm_str ))
				text('*');
		else
				text(' ');
		goto_mark;
		switch_window( owin );
		insert_mode = ti;
}

macro notes_create
{
		str nstr[80] = global_str('~NOTEBOOK_PARMS');
		str tstr[100];

		int main_window = parse_int('/MW=', nstr ),
				category_window = parse_int('/CW=', nstr ),
				note_list_window = parse_int('/NW=', nstr ),
				edit_window = parse_int('/EW=', nstr ),
				owin = cur_window,
				tpb = persistent_blocks
				;

		int jx, jy;

		persistent_blocks = TRUE;

		if(owin == category_window) {
			jx = c_line;
			jy = jx;
			return_str = '';
			RM('USERIN^QUERYBOX /H=NOTEBOOK^CATEGORIES%Modify Category/T=NEW NOTE CATEGORY/W=20');
			if( return_int ) {
				switch_window( main_window );
				tof;
				search_fwd( '|12|12|12CATEGORIES', 0 );
				while( jx-- > 0)
						down;
				cr;
				up;
				put_line( '   ' + shorten_str(return_str) );
				jy = c_line;
				switch_window( category_window );
				goto_line(jy - 2);
				goto_col(1);
        jy = line_attr;
				cr;
        line_changed = FALSE;
        line_attr = jy;
				up;
				mark_pos;
				put_line( '   ' + return_str );
				rm( 'n_get_list');
				switch_window( owin );
				goto_mark;
			}
		} else if (owin == note_list_window ) {
			switch_window( edit_window );
			return_str = date + ' ' + str_del( time, 6, 3 );
			return_str = '|127CD=' + return_str + '|127MD=' + return_str;
			erase_window;
			persistent_blocks = tpb;
			rm('n_edit_note');
			persistent_blocks = TRUE;
			if( return_int ) {
				int		tl,
							to_do = parse_int('|127DO=', return_str ) && !parse_int('|127DONE=', return_str);
				tof;
				block_begin;
				eof;
				block_end;
				switch_window( main_window );
				goto_line( global_int('~NOTEBOOK_LINE') );
				goto_col(1);
				down;
				reg_exp_stat = TRUE;
				if( !search_fwd('%|12',0) ) {
					eof;
					if( c_col > 1)
							down;
				}
				goto_col(1);
				cr;
				up;
				jx = global_int( '~NOTEBOOK_NUM' );
				++jx;

				put_line( '|12NUM=' + str(jx) + '|12' + return_str );
				set_global_int( '~NOTEBOOK_NUM', jx );
				down;
				window_copy( edit_window );
				block_off;
				rm("sort_note");
				tl = return_int;
				RM('n_list_entry /N=' + str(jx) );
				put_line( return_str );
				switch_window( note_list_window );
				goto_line( tl );
				goto_col( 1 );
				if( !at_eof) {
          tl = line_attr;
					cr;
          line_changed = FALSE;
          line_attr = tl;
					up;
				}
				put_line( return_str );
        line_changed = FALSE;

				if( to_do ) {
          line_attr = cb_s_color;
					rm('n_set_cat /ON=1');
				} else {
					reg_exp_stat = TRUE;
					mark_pos;
					tof;
					to_do = search_fwd('%?@*', 0);
					rm('n_set_cat /ON=' + str(to_do) );
					goto_mark;
				}
			}
		}
		switch_window( owin );
		persistent_blocks = tpb;
}


macro n_edit_note
{
	int menu = menu_create;
	int start = 1;

	if(parse_int('/EDIT=', mparm_str))
			start = 4;

	wrap_stat = TRUE;
	right_margin = 65;
	block_off;
	menu_set_item( menu, 1, 'Subject:', parse_str('|127SBJ=', return_str), '/QK=1/L=1/C=1/W=40', 0, 0, 0 );
	menu_set_item( menu, 2, 'to Do    ', '', '/QK=4/L=1/C=55', 13, parse_int('|127DO=', return_str), 0 );
	menu_set_item( menu, 3, 'Complete ', '', '/QK=1/L=2/C=55', 13, parse_int('|127DONE=', return_str), 0 );
	menu_set_item( menu, 4, 'Text:', '', '/QK=1/L=4/C=1/W=68/HT=11/WIN=' + str(cur_window), 16, 0, 0 );
	menu_set_item( menu, 5, 'OK', '', '/KC=<F10>/K2=68/L=18/C=1/R=1', 11, 0, 0 );
	menu_set_item( menu, 6, 'Cancel', '', '/KC=<ESC>/K2=27/K2=1/L=18/C=10/R=0', 11, 0, 0 );
	menu_set_item( menu, 7, 'Help', '', '/KC=<F1>/K2=59/L=18/C=60/R=2', 11, 0, 0 );
	menu_set_item( menu, 8, 'Created', parse_str('|127CD=', return_str), '/W=16/L=16/C=1', 0, 0, 0 );
	menu_set_item( menu, 9, 'Last mod', parse_str('|127MD=', return_str), '/W=16/L=16/C=30', 0, 0, 0 );
	return_int = menu;
	rm('USERIN^DATA_IN /H=NOTEBOOK^EDIT_NOTE/T=EDIT NOTE/HN=1/#=9/S=' + str(start));
	return_str = '|127SBJ=' + menu_item_str( menu, 1, 2 )
				+ '|127DO=' + str(menu_item_int( menu, 2, 2 ))
				+ '|127DONE=' + str(menu_item_int( menu, 3, 2 ))
				+ '|127CD=' + menu_item_str( menu, 8, 2 )
				+ '|127MD=' + menu_item_str( menu, 9, 2 );
	menu_delete( menu );
}


macro n_get_list
{
		str nstr[80] = global_str('~NOTEBOOK_PARMS'),
		    tstr[20];

		int cat_count, jx,
				main_window = parse_int('/MW=', nstr ),
				category_window = parse_int('/CW=', nstr ),
				note_list_window = parse_int('/NW=', nstr),
				owin = cur_window,
				tpb = persistent_blocks
				;

		persistent_blocks = TRUE;
		switch_window( category_window );
		goto_col(1);
		if(at_eof)
				tof;
		if(at_eof) {
			put_line('   Default');
			switch_window( main_window );
			tof;
			down;
			eol;
			cr;
			put_line('   Default');
			switch_window( category_window );
		}
		return_str = copy( get_line, 4, 20 );
		tstr = return_str;
		pad_str( tstr, 20, ' ' );
		mark_pos;
		tof;
		reg_exp_stat = TRUE;
    while( search_fwd( '%|175', 0) ) {
			jx = line_attr;
      insert_mode = FALSE;
      text(" ");
      insert_mode = TRUE;
			line_changed = false;
			line_attr = jx;
		}
		goto_mark;
		jx = line_attr;
		put_line( '|175' + copy( get_line, 2, 22 ) );
		line_changed = false;
		line_attr = jx;
		block_begin; block_end;
		// menu_set_str( global_int('~NOTEBOOK_MENU'), parse_int('/CAT#=', nstr), 1, tstr );
		switch_window( main_window );
		block_off;
		reg_exp_stat = false;
		tof;
		if( search_fwd( '|12|12' + return_str + '~~~!', 0 )) {
			set_global_int('~NOTEBOOK_LINE', c_line );
			down;
			cat_count = c_line;
			block_begin;
			reg_exp_stat = TRUE;
			if( search_fwd( '%|12', 0 )) {
				up;
			} else {
				eof;
			}
			block_end;
			if( (block_line1 < cat_count) || ( block_line2 < cat_count ))
					block_off;
		} else {
			eof;
			if( c_col != 1)
					down;
			put_line( '|12|12' + return_str + '~~~!' );
			set_global_int('~NOTEBOOK_LINE', c_line );
			down;
		}
		switch_window( note_list_window );
		erase_window;
		window_copy( main_window );
		block_off;
		goto_col(2);
		while( !at_eof ) {
			if( cur_char == '*' ) {
				line_attr = cb_s_color;
				line_changed = FALSE;
			}
			down;
		}
		tof;
    while(search_fwd("%¯",0)) {
			insert_mode = FALSE;
			text(" ");
      line_changed = FALSE;
			insert_mode = TRUE;
		}
    jx = C_LINE;
    tof;
    while(c_line < jx )
      down;
		switch_window( owin );
		persistent_blocks = tpb;
}

macro n_get_categories
{
		str nstr[80] = global_str('~NOTEBOOK_PARMS');

		int cat_count,
				main_window = parse_int('/MW=', nstr ),
				category_window = parse_int('/CW=', nstr ),
				owin = cur_window,
				tpb = persistent_blocks
				;

		persistent_blocks = TRUE;

		switch_window( main_window );
		block_off;
		reg_exp_stat = true;
		tof;
		if( search_fwd( '%|12|12|12CATEGORIES', 0 )) {
			down;
			cat_count = c_line;
			block_begin;
			if( search_fwd( '%|12', 0 )) {
				up;
			} else {
				eof;
			}
			block_end;
			if( (block_line1 < cat_count) || ( block_line2 < cat_count ))
					block_off;
		} else {
			cr;
			up;
			put_line( '|12|12|12CATEGORIES' );
			down;
			cat_count = c_line;
		}
		switch_window( category_window );
		erase_window;
		window_copy( main_window );
		goto_col(2);
		while( !at_eof ) {
			if( cur_char == '*' ) {
				line_attr = cb_s_color;
				line_changed = FALSE;
			}
			down;
		}
		tof;
		block_off;
		switch_window( owin );
		persistent_blocks = tpb;
}
