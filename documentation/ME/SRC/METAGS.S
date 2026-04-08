macro_file metags;
/*-----------------09-18-91 03:28pm-----------------
 MULTI-TAGS
 Multiple language tagging.

 01-12-94 01:52pm [scm]
			Fixed wild-card tag scanning problem with navigating file/dir boxes
			and processing the correct files from the mask.  It should be noted
			that the DATA_IN type 20 field requires the /SDD and /MSK args to point
			to the correct fields, if used. They werent in this case.

 05-27-93 11:24am
      Fixed some addition C parsing problems.

 04-13-93 02:45am
			Revamped the C/C++ parsing.
			Added int,str,real,void function types to CMAC.
			Fixed bug with Next Tag.

 12-14-92 02:18pm DKF
			Added 'EVOLVE' as an XBASE type for Multi-Edit 6.xx specific Evolve

 12-01-92 10:22am TMJ
      Fixed several bugs.  Added tag prompt.

 08-31-92 10:45am DPC
      Separated structure type and name tags. Fixed bug in match routine
      that skipped the first character after a closing comment. Fixed a
      bug with subdirectory searches created in previous fix.  Fixed problems
      with '\' line continuation character used within a comment.

 07-17-92 08:42am DPC
      Made possible to parse multiple definitions using ',' separator and
      to parse int declarations using only 'signed' or 'unsigned'.  Fixed
      various little bugs.  Modified #define to accomodate \ line terminator.
      Modified to capture arrays of structures and typeless structures.

 07-15-92 08:11am DPC
      Fixed bug in db, dw and dd that was introduced by yesterdays fixes.

 07-14-92 02:47pm DPC
      Fixed problems created when transferring code from old version without
      checking what changed were made in new version.  Fixed problem with
      re-scanning of files in subdirectories.  Fixed problem with match_else
      routine with regard to #if defined() types.  Fixed problem with Borland
        extrn "C" {
      causing the borland windows.h file not to be scanned.

      All lines added or modified by BPC have //DPC comments.  Not all
			deleted lines are indicated.

 07-14-92 09:42am DPC
      Modified to handle equ, db, dw, dd for assembly code.  Also modified
      assembly to include tag type and have tags in same fields as C code.
      Modified to handle global variables, procedures returning procedure
      pointers, and typedefs of arrays, and procedures in C code.

 02-28-92 06:57pm
			Fixed 2 small bugs with Browse.

 11-03-91 04:21pm
			Added "BROWSE" box feature to "Find tag" and "Tag List".

 10-15-91 11:38am
			Modifed XBASE support to handle PROC and FUNC abbreviations
			Added Cross-Directoried tagging.

 09-23-91 04:40pm
			Added XBASE, PAL and ASM.

 09-20-91 03:31pm
			Added configuration dialog box.


 09-20-91 11:14am
			Added ability to define different tag files
			depending on the language being used.

 09-19-91 12:32pm
			Added ability to Delete single tags, or whole
			files from the tag database via the List
			dialog box.


 GLOBAL VARS:

global_str('TAG_F_' + language )
	This lets you assign tag files based on the
	language you are using.
	For example: set_global_str('TAG_F_C', 'C.TAG' );

		global_str('METAGS_FILE_NAME')
				This lets you change the default tag file name
				(METAGS.TAG).
 --------------------------------------------------*/


#define TG_Start_File "ÄÄ´ "
#define TG_End_File " Ã"
#define metags_version "1.1a"

#define tag_delimits word_delimits + "³´µ¶·¸¹º»¼½¾¿ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞß.->"
#define tag_language caps( parse_str( '|127LS=', global_str( '.' + get_extension(file_name))))
#define tag_format_line "                                                         "

/*-----------------09-15-91 11:23pm-----------------
 * Executes the main tag menu
 *--------------------------------------------------*/
macro metags {
	rm('menu /MN=METAGS' + mparm_str );
}

macro _MN_METAGS  {
	int menu = return_int;
	int menu_count =  0;
	if( global_str("METAGS_CONFIG") == "" ) {
		rm('tag_init');
	}
	return_int = menu;
	menu_set_item( return_int, ++menu_count, "Find tag under cursor", global_str("!TAG_KEY1"), "/S=2/H=/NM=1/ CMD=tag_locateTYPE=1CS=0", 0, 0, 0);
	menu_set_item( return_int, ++menu_count, "find Again", global_str("!TAG_KEY2"), "/S=2/H=/NM=1/ CMD=tag_locate /NEXT=1TYPE=1CS=0", 0, 0, 0);
	menu_set_item( return_int, ++menu_count, "List tags...", global_str("!TAG_KEY3"), "/S=2/H=/NM=1/ CMD=tag_listTYPE=1CS=0", 0, 0, 0);
  menu_set_item( return_int, ++menu_count, "Prompt for tag...", global_str("!TAG_KEY11"), "/S=2/H=/NM=1/ CMD=tag_locate /MAN=1TYPE=1CS=0", 0, 0, 0);
  menu_set_item( return_int, ++menu_count, "Browse current file...", global_str("!TAG_KEY4"), "/S=2/H=/NM=1/ CMD=tag_browse_fileTYPE=1CS=0", 0, 0, 0);
	#IFDEF object_tree
  menu_set_item( return_int, ++menu_count, "Object hierarchy of tag...", global_str("!TAG_KEY9"), "/S=2/H=/NM=1/ CMD=tag_obj_cursorTYPE=1CS=0", 0, 0, 0);
	#ENDIF
  menu_set_item( return_int, ++menu_count, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0, 0);
  menu_set_item( return_int, ++menu_count, "Scan tags for current file", global_str("!TAG_KEY5"), "/S=2/H=/NM=1/ CMD=tag_buildTYPE=1CS=0", 0, 0, 0);
  menu_set_item( return_int, ++menu_count, "Wildcard tag scan...", global_str("!TAG_KEY6"), "/S=2/H=/NM=1/ CMD=TAG_SCAN_FILESTYPE=1CS=0", 0, 0, 0);
  menu_set_item( return_int, ++menu_count, "Update tags for current file", global_str("!TAG_KEY7"), "/S=2/H=/NM=1/ CMD=TAG_UPDATETYPE=1CS=0", 0, 0, 0);
  menu_set_item( return_int, ++menu_count, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0, 0);
  menu_set_item( return_int, ++menu_count, "Configure...", "", "/S=2/H=/NM=1/ CMD=tag_configureTYPE=1CS=0", 0, 0, 0);
  return_str = "/#=" + str(menu_count) + "/T=MULTI-TAGS " + metags_version + "/H=METAGS^*";
}


macro tag_find_file
/* ************************************************************************
 * Locates and/or loads the correct tag file for the current file
 *
 * Parameters:
 * 							/C=1		Create new tag file and file specifier
 * 											 if old one not found.
 * 							/F=1		Move cursor to file specifier for current file
 *              /TF=str Overide the tag filename to use
 *              /FN=str Overide the current window filename to use
 *              /DT=int Date/time to use.
 *
 * Returns:
 * 							RETURN_INT = 1  if successful
 * 												 = 0  if not
 *
 * ********************************************************************** */
{
	str 	fn[128] = parse_str( "/FN=", mparm_str ),
				fp[80],
				tag_file_name[128] = parse_str( "/TF=", mparm_str ),
				ls[80]
				;

	int 	f = parse_int('/F=', mparm_str),
				c = parse_int('/C=', mparm_str),
				lft, j1, j2 , j3, jx;

	if( global_str("METAGS_CONFIG") == "" ) {
		rm('tag_init');
	}

	if( fn == "" ) {
		fn = file_name;
	}
	ls = caps( parse_str( '|127LS=', global_str( '.' + get_extension(fn))));
	if( (tag_file_name == "") && (truncate_path(file_name) == "OBJTREE.TMP") ) {
		tag_file_name = parse_str("/TF=", global_str("@OBJ_TREE_PARMS@") );
	}
	if( tag_file_name == "" )
			tag_file_name = global_str('TAG_F_' + ls );
	if( tag_file_name == '' ) {
		tag_file_name = Global_Str('METAGS_FILE_NAME');
		if( tag_file_name == '' )
			tag_file_name = 'METAGS.TAG';
	}
	return_int = TRUE;
	tag_file_name = fexpand( tag_file_name );
	if( !switch_file( tag_file_name ) ) {
		set_global_int("~OBJ_TREE_BUILD", 1 );
		switch_window( window_count );
		create_window;
		load_file( tag_file_name );
		if( error_level != 0 )
			file_name = tag_file_name;
		error_level = 0;
		return_int = TRUE;
	}
	format_line = tag_format_line;
	return_str = truncate_path(fn);
	fp = fexpand(get_path(fn));
	if( f || c) {
		tof;
	search_again:
		if( search_fwd( '%' + TG_Start_File + return_str + ' ', 0 ) ) {
			ls = parse_str("þPATH=", get_line );
			if( (ls != "") && (ls != fp) ) {
				eol;
				goto search_again;
			}
			if(c) {
				block_begin;
				down;
				if( search_fwd( '%' + TG_Start_File, 0 ) ) {
					up;
				} else {
					eof;
				}
				block_end;
				delete_block;
			}
			return_int = TRUE;
		} else
			return_int = FALSE;
	}
	if(c) {
		eof;
		if(c_col > 1 ) {
			goto_col(1);
			forward_till_not(' ');
			if(cur_char != "\xFE" ) {
				down;
				goto_col(1);
			}
		}
		if( c_line > 1 ) {
			put_line("                                                            \xFE");
			down;
		}

		lft = parse_int('/DT=', mparm_str );
		if( lft == 0 ) {
			j1 = first_file( fn );
			if(  j1 == 0  )
				lft = last_file_time;
		}
		fn = return_str;
		if( lft != 0 ) {
			jx = lft & $FFFF;
			j1 = jx >> 11;
			j2 = (jx >> 5) & $3F;
			return_str = Str_Del(Make_Time_Str( j1, j2, 0 ),6,3);

			jx = last_file_time >> 16;
			j1 = (jx >> 9) + 1980;
			j2 = (jx >> 5) & $0F;
			j3 = (jx & 31);
			return_str = Make_Date_Str( j1, j2, j3 ) + ' ' + return_str;
		} else {
			return_str = date + ' ' + str_del( time, 6, 3 );
		}

		pad_str( fn, 13, ' ' );

		if ( parse_int("/SP=", global_str("METAGS_CONFIG") )  )
			tag_file_name = "þPATH=" + fp;
		else
			tag_file_name = "";

		put_line(TG_Start_File + fn  + TG_End_File + "ÄÄÄÄ" + TG_Start_File + return_str + TG_End_File + 'ÄÄÄÄÄÄÄþDT=' + str(lft) + tag_file_name );
		down;
		return_int = TRUE;
	}
	window_attr = $81;
exit:
}


macro tag_browse_file
	trans2
{
	int				src_win = window_id,
						tag_win,
						work_win = 0
						;
	str fn[80] = file_name;

again:
	refresh = FALSE;
	working;
	rm("tag_find_file /F=1");
	working;
	if(!return_int) {
		switch_win_id( src_win );
		rm("tag_update");
		make_message("");
		working;
		rm("tag_find_file /F=1");
		if(!return_int) {
			rm("messagebox /B=2/T=ERROR/M=Unable to find or build tag table.");
			goto exit;
		}
	}
	block_begin;
	eol;
	if( !search_fwd("%" + TG_Start_File,0) ) {
		eof;
	}
	else
		up;
	block_end;
	tag_win = cur_window;
	switch_window(window_count);
	create_window;
	format_line = tag_format_line;
	work_win = window_id;
	window_copy( tag_win );
	block_off;
	down;
	{
		int menu = menu_create;
		menu_set_item( menu, 1, "", "", "/L=1/C=1/W=53/WIN=" + str( cur_window ), 15, 0, 0 );
		menu_set_item( menu, 2, "re-Scan file", "", "/QK=4/L=2/C=58/R=10", 11, 0, 0 );
		menu_set_item( menu, 3, "Go", "", "/KC=<ENTER>/QK=1/K1=13/K2=28/L=6/C=58/R=1", 11, 0, 0 );
		menu_set_item( menu, 4, "Cancel", "", "/KC=<ESC>/QK=1/K1=27/K2=1/L=8/C=58/R=0", 11, 0, 0 );
		menu_set_item( menu, 5, "Help", "", "/KC=<F1>/QK=1/K2=59/L=11/C=58/W=48/R=2", 11, 0, 0 );
		return_int = menu;
		rm("data_in /HN=1/#=5/T=Browsing " + truncate_path(fn) + "/H=metags^BROWSE");
		menu_delete( menu );
		if( return_int == 10 ) {
			delete_window;
			switch_win_id( src_win );
			rm("tag_update");
			goto again;
		}
		if( return_int ) {
      rm("tag_goto /EB=0/OW=" + str(src_win));
			switch_win_id( work_win );
		}
	}
	delete_window;
	switch_window(tag_win);
	block_off;
exit:
	switch_win_id( src_win );
}


macro tag_locate
/* ---------------08-22-91 10:03am-------------------
 * Grabs the word under the cursor and attempts to
 * locate it in the tag file.
 *
 * Parameters:
 *              /MAN=1   Prompt for tag text
 * 							/NEXT=1  Do not go to the top of the
 * 											 tag file, instead find the
 * 											 next occurance of the last
 *                       tag locate.
 *              /FO=1    Just locate the spot in the
 *                       tag file, do NOT locate source
 *              /TF=filename  The tag filename to use
 *                        (do not auto determine)
 -------------------------------------------------- */
{
	str       tag_id;

	int				src_win = window_id,
						tag_win,
						next_tag = parse_int("/NEXT=", mparm_str);

again:
	refresh = false;
		// If we are doing a NEXT tag then do not start from the top,
		// and use the last tag instead of the current word
	if( !next_tag ) {
    if( parse_int("/MAN=",mparm_str))
    {
      return_str = "";
      rm("USERIN^QUERYBOX /P=Enter Tag:/W=32/ML=80/T=Multi-Tags/H=METAGS^PROMPT");
      if( return_int <= 0 )
        goto exit;
    }
    else
    {
      rm("tag_parse_text");
    }
		tag_id = return_str;
		if( tag_id == "" ) {
			make_message( "No valid tag." );
			switch_win_id( src_win );
			goto exit;
		}
	}

	rm( 'tag_find_file /TF=' + parse_str("/TF=", mparm_str) );
	if( !return_int ) {
			make_message( 'No tag file found.' );
			switch_win_id( src_win );
			goto exit;
	}

	if( !next_tag ) {
		tof;
	} else {
		tag_id = global_str( 'LAST_TAG_FIND' );
		eol;
	}
	ignore_case = TRUE;
	reg_exp_stat = TRUE;
	if( search_fwd( '%' + tag_id + '[ |9]', 0 ) ) {
		set_global_str('LAST_TAG_FIND', tag_id );
		return_int = 1;
		if( !parse_int("/FO=", mparm_str )) {
			RM('tag_goto /OW=' + str( src_win ));
			if( return_int == 2  )
			{
				next_tag = TRUE;
				goto again;
			}
		}
	} else {
		make_message( 'No tag found.' );
		switch_win_id( src_win );
		return_int = 0;
	}
exit:
}

macro tag_parse_text
{
	str ls[40] = tag_language;

	mark_pos;

again:
	return_str = "";
	right;
	while( (c_col > 1) && xpos(cur_char, tag_delimits, 1) )
		left;
	while( (c_col > 1) && !xpos(cur_char, tag_delimits,1) )
		left;
  /*
	if(( ls == "C") || (ls == "C++") ) {
		if( cur_char == "." ) {
			left;
			while( (c_col > 1) && !xpos(cur_char, tag_delimits,1) )
				left;
			if( xpos(cur_char, tag_delimits,1 ))
				right;
			return_str = shorten_str(get_word( tag_delimits )) + ".";
		}
	}
  */
	if( xpos(cur_char, tag_delimits,1 ))
		right;
	return_str += shorten_str(get_word( tag_delimits ));
	if( return_str == "" ) {
		word_right;
		if( !at_eol ) {
			return_str = shorten_str( get_word( tag_delimits ) );
		}
	}
	if( (ls == 'C') || (ls == 'C++') ) {
		if( xpos( ' ' + return_str + ' ', ' class struct union int void typedef ', 1 ) ) {
			word_right;
			while( at_eol && !at_eof )
				word_right;
			goto again;
		}
	}
	goto_mark;
}

macro tag_goto
{
	int eb = parse_int("/EB=", global_str("METAGS_CONFIG"));
	int vw = parse_int("/VW=", mparm_str),
			create_overide = parse_int("/CR=", mparm_str);
	str ls[80];
	int jx, twin = parse_int("/OW=", mparm_str);
	int old_win = 0, result = 0,
			bc = box_count, use_path = FALSE;
	str fn[80], tag_id[80], tag_str, tstr, sstr;
	working;
	mark_pos;
	if ( xpos("/EB=", mparm_str,1) )
	{
		eb = parse_int("/EB=", mparm_str);
	}
	if ( xpos("/BC=", mparm_str,1) )
	{
		bc = parse_int("/BC=", mparm_str);
	}
	if( search_bwd( '%' + TG_Start_File, 0 ) ) {
		forward_till_not('Ä´ ');
		fn = get_word(' ');
		fn = parse_str("þPATH=", get_line ) + fn;
		if( get_path(fn) != "" )
				use_path = TRUE;
	}
	goto_mark;
	first_word;
	tag_id = get_word( ' |9|255' );
	goto_col( 42 );
	forward_till( ' |9|255' );
	forward_till_not(' |9|255');
	if( cur_char == '' ) {
		right;
		forward_till('');
		right;
		forward_till_not(' |9|255');
	}
	mark_pos;
	tag_str = shorten_str( get_word('') );		// Get the literal string
	goto_mark;
	sstr = '';																// Now parse the search string
	while( !at_eol ) {
		sstr = sstr + make_literal(get_word(' |9|255'));
		if( !at_eol ) {
			if( cur_char == ' ' ) {
				right;
				if( !xpos( cur_char, ' |9|255', 1 ) ) {
					sstr = sstr + ' ';
					continue;
				}
			}
			forward_till_not(' |9|255' );
			if( !at_eol )
				sstr  = sstr + '[ |9|255]+';
		}
	}
	goto_col(1);
	jx = 1;
	crunch_tabs( tag_str, jx );
	tabs_to_spaces( tag_str );
	tag_str = remove_space( tag_str );

  switch_win_id( twin );
  if( truncate_path( file_name ) == truncate_path(fn) ) {
    if( !use_path || (get_path(file_name) == get_path(fn))) {
      old_win = cur_window;
      goto skip_win_search;
    }
  }

	for( jx = 1; jx <= window_count; jx++ ) {
		switch_window( jx );
		if( !(window_attr & $80) ) {
			if( truncate_path( file_name ) == truncate_path(fn) ) {
				if( !use_path || (get_path(file_name) == get_path(fn))) {
					old_win = cur_window;
					break;
				}
			}
		}
	}
skip_win_search:
	if( (vw == 0) || create_overide )
		rm( 'CREATEWINDOW' );
	else
		switch_window( vw );

	if( old_win != 0 ) {
		if( cur_window != old_win )
			link_window( old_win );
		tof;
	}
	else
	{
		make_message( str(use_path) );
		return_str = fn;
		rm( 'LDFILES' );
	}
	reg_exp_stat = false;
go:
	result = 1;
	call do_search;
	if( eb ) {
		int x1 = win_x1, y1 = win_y1, x2 = win_x2, y2 = win_y2, old_attr = window_attr;
		jx = read_only;
		read_only = TRUE;
		set_global_str("!METAGS_NEXT", "/R=10/QK=2/T=nExt");
		rm("USERIN^EditWindow /T=Browsing " + file_name +
				"/EV1=!METAGS_NEXT/X=4/Y=4/W=70/L=15/A=1/H=metags^*" );
		set_global_str("!METAGS_NEXT", "");
		read_only = jx;
		result = return_int;
		refresh = false;
		result = 1;
		if( return_int == 1 ) {
			while ( box_count > bc )
			{
				kill_box;
			}
			if( old_win ) {
				mark_pos;
				delete_window;
				switch_window( old_win );
				goto_mark;
			}
			else {
				window_attr = 0;
				rm("EXTSETUP");
				size_window( x1, y1, x2, y2 );
				read_only = jx;
				window_attr = old_attr;
			}
		}
		else
		{
			if(return_int == 10)
				result = 2;
			delete_window;
			switch_win_id( twin );
		}
	}
	else
	{
		if( old_win && (!vw || create_overide) ) {
			mark_pos;
			delete_window;
			switch_window( old_win );
			goto_mark;
		}
	}
	if( window_attr & $40 ) {
		RM('WINDOW^ZOOM');
	}
	goto exit;

do_search:
		reg_exp_stat = TRUE;

		ls = caps( parse_str( '|127LS=', global_str( '.' + get_extension(fn))));

			// Special processing for Turbo-Pascal files
		if (ls == "PASCAL") {
			ignore_case = TRUE;
			if( parse_int("/PI=", global_str("METAGS_CONFIG") ) ) {
				if( ( caps(copy( sstr, 1, 8 )) == "FUNCTION") ||
						( caps(copy( sstr, 1, 9 )) == "PROCEDURE")) {
					if( search_fwd("IMPLEMENTATION", 0) ) {
						int jy;

						tabs_to_spaces( sstr );
						sstr = remove_space( sstr );
						jx = xpos( " ", sstr, 1 );
						do
						{
							++jx;
							if( (jy = xpos(str_char(sstr,jx)," ;(", 1)) ) {
								sstr = copy( sstr, 1, jx );
							}
						} while((jy == 0) && (jx < svl(sstr)));
						if( search_fwd( sstr, 0 ) ) {
							make_message( "Tag found" );
							ret;
						}
						goto second_shot;
					}
				}
			}
		}

		while( search_fwd( sstr, 0 ) ) {
			jx = c_col;
			first_word;
			if( jx != c_col ) {
				eol;
				continue;
			}

			if( length( tag_str) >= 128 ) {
				make_message("Tag found");
				ret;
			}
			mark_pos;
			tstr = get_word('');
			goto_mark;
			crunch_tabs( tstr, jx );
			tabs_to_spaces( tstr );
			if( length( remove_space( tstr) ) == length( tag_str ) ) {
				make_message("Tag found");
				ret;
			}
			eol;
		}
	second_shot:
		tof;
		while( search_fwd( tag_id, 0 ) ) {
			if( c_col > 1 ) {
				left;
				if( !xpos(cur_char, '|9|255 ~!@#$%^&*()_+||-=\{}[]:;''",.<>/?', 1) ) {
					right; right;
					continue;
				}
				right;
			}
			mark_pos;
			goto_col( c_col + length(tag_id) );
			if( !at_eol ) {
				if( !xpos(cur_char, '|9|255 ~!@#$%^&*()_+||-=\{}[]:;''",.<>/?', 1) ) {
					goto_mark;
					right;
					continue;
				}
			}
			goto_mark;
			make_message("Tag string not found, found Tag ID instead.");
			ret;
		}

	ret;


exit:
	return_int = result;
	reg_exp_stat = true;
}

macro tag_right_mouse trans2 {
	int t_pb = persistent_blocks, t = system_timer,
			cx = mou_last_x,cy = mou_last_y, k1 = key1, k2 = key2
			;
	persistent_blocks = TRUE;

	RM('MOUSE^MOUSEEVENT2');

	if( (cx == mou_last_x) && (cy == mou_last_y) ) {
		do
			if( check_key ) {
				if(( key1 == k1) && (key2 == k2)) {
					if( (cx == mou_last_x) && (cy == mou_last_y)) {
						rm("tag_locate");
						if( !return_int ) {
							rm("MEHELP /EXTHELP=1/CX=1");
						}
					}
				}
				break;
			}
		while( (system_timer - t) < 18 );
	}


	persistent_blocks = t_pb;
}


macro tag_update
{
	int tw = cur_window,
			tt = 0, tn
			;

	refresh = false;
	if( file_changed ) {
		rm('meutil1^savefile /NP=1');
		if ( !return_int )
		{
			goto exit;
		}
	}
	if( first_file( file_name ) == 0 )
		tt = last_file_time;

	rm('tag_find_file /F=1');
	if( return_int ) {
		tn = parse_int('þDT=', get_line );
		if( tt > tn ) {
			switch_window( tw );
			rm('tag_build');
			make_message('Tags updated.');
		} else {
			make_message('File does not require updating.');
		}
	} else {
		switch_window( tw );
		rm('tag_build');
	}

exit:
	switch_window( tw );
}




macro tag_list
/*-----------------09-18-91 12:51pm-----------------
 Brings up a list box of the current tag file.
 --------------------------------------------------*/
//  no_break
	trans2
{
	int menu, start = 1;
	int tw, nb = 0,	fw, mh, view_win;
	int tundo = undo_stat;
  int tkh = keyword_highlighting;
	int ow = window_id,
			bc = box_count,
			jx,tl,
			f_count
			;
	str ls[40], p[80], fn[80] = file_name;
  keyword_highlighting = FALSE;
	working;
	refresh = false;
	rm( 'tag_find_file' );
again:
	if( return_int )
	{
		menu = menu_create;
		undo_stat = FALSE;
		tw = cur_window;
		switch_window( window_count );
		create_window;
		screen_num = 0;
		fw = cur_window;
		switch_window( window_count );
		create_window;
		screen_num = 0;
		view_win = cur_window;
		read_only = TRUE;
		switch_window( tw );
		screen_num = 0;
		mark_pos;
		tof;
		reg_exp_stat = true;
		ignore_case = FALSE;
		f_count = 0;
		while( search_fwd( "%" + TG_Start_File , 0 ) ) {
			++f_count;
			line_changed = FALSE;
			line_attr = cb_s_color;
			return_str = get_line;
			p = parse_str('|254PATH=', return_str);
			return_str = shorten_str( copy( return_str, 5, 12 ) );
			put_line_to_win( return_str + '               |127L=' + str(c_line) + '|127P=' + p, f_count, fw, false );
			down;
			goto_col( 1 );
		}
		eof;
		jx = c_line;
		tof;
		undo_stat = tundo;
		ignore_case = TRUE;
		switch_window( fw );
		tof;
		search_fwd("%" + truncate_path( fn ) + " ",0);
		switch_window( tw );
		goto_mark;
		goto_col(1);
		mh = (screen_length - 8) / 2;
		menu_set_item( menu, 1, 'Files', '', '/QK=1/DC=1/W=13/C=12/L=1/HT=9/WIN=' + str(fw) +
																			'/DISPMAC=TAG_LIST_DISPLAY //XM=0//TW=' + str(tw) + '//FW=' + str(fw),
																			15, 0, 0 );
		menu_set_item( menu, 2, 'Tags', '', '/QK=1/W=50/DC=1/C=27/L=1/HT=' + str(mh + 1) + '/WIN=' + str(tw) +
																			'/DISPMAC=TAG_LIST_DISPLAY //XM=4//TW=' + str(tw) + '//FW=' + str(fw),
																			15, 0, 0 );
		menu_set_item( menu, 3, 'View: ', '',
		'/QK=1/W=50/DC=1/C=27/OL=1/L=' + str( mh + 3 ) + '/HT=' + str(mh - 1) + '/WIN=' + str(view_win), 16, 0, 0 );
		// menu_set_item( menu, 3, '', '', '/C=1/L=' + str( 3 + mh), 10, 0, 0 );
		menu_set_item( menu, 4, 'Load', '',
				'/QK=1/KC=<F10>/K2=68/R=22/C=1/L=1',11, 0, 0 );
		menu_set_item( menu, 5, 'Done', '',
				'/KC=<ESC>/K1=27/K2=1/R=0/C=1/L=3',11, 0, 0 );
		menu_set_item( menu, 6, 'Help', '',
				'/QK=1/KC=<F1>/K2=59/R=2/C=1/L=5',11, 0, 0 );
		menu_set_item( menu, 7, 'taG file', '',
				'/QK=3/R=21/C=1/L=7',11, 0, 0 );
		menu_set_item( menu, 8, 'Delete',
				'TAG_LIST_DISPLAY /XM=1/TW=' + str(tw) + '/FW=' + str(fw),
				'/KC=<DEL>/K2=83/R=20/C=1/L=11/M=1',11, 0, 0 );
		menu_set_item( menu, 9, 'View', 'tag_list_display /XM=2/VMN=3/VW=' + str( view_win ) +
										'/TW=' + str(tw) + '/FW=' + str(fw) + '/MENU=' + str(menu),
				'/KC=<ENTER>/K1=13/M=1/K2=28/R=1/C=1/L=13',11, 0, 0 );
		menu_set_item( menu, 10, 'Update tags', '',
				'/QK=1/R=23/C=1/L=15',11, 0, 0 );

	#IFDEF object_tree
    menu_set_item( menu, 11, 'Obj Tree', 'tag_list_display /XM=3/VMN=3/VW=' + str( view_win ) +
                   '/TW=' + str(tw) + '/FW=' + str(fw) + '/MENU=' + str(menu),
             '/M=1/QK=1/R=13/C=15/L=11',11, 0, 0 );
    menu_set_item( menu, 12, 'All    ', '',
        '/QK=1/C=14/L=13',13, (Global_Int("~OBJ_TREE_MODE") AND 1) != 0, 0 );
    menu_set_item( menu, 13, 'Condns ', '',
        '/QK=1/C=14/L=14',13, (Global_Int("~OBJ_TREE_MODE") AND 2) != 0, 0 );

	#ENDIF
		return_int = menu;
		rm( "USERIN^DATA_IN /ABT=View/A=2/NK=1/NB=' + str(nb) + '/POSG=!TAG_LIST_POS/HN=1/NOW=1/S=" + str(start) +
                      "/H=metags^LIST" +
									 #IFDEF object_tree
											"/#=13" +
									 #ENDIF
											"/#=10" +


											"/T=TAGS: " + file_name );
		start = global_int("!DATA_IN_#");
		refresh = FALSE;
		jx = cur_window;
		switch_window( view_win );
		window_attr = 0;
		return_str = global_str("~TAG_VIEW_RO");
		if( parse_int("/BID=", return_str) == buffer_id ) {
			read_only = parse_int("/RO=", return_str );
		}
		switch_window( tw );
		window_attr = 0x81;
		switch_window( fw );
		window_attr = 0x81;
		file_changed = false;
		switch_window( jx );
		if( return_int == 22 )
		{
			if( (cur_window == tw) || (cur_window == fw ) ) {
				rm('tag_list_display /XM=2/VMN=3/VW=' + str( view_win ) +
							'/TW=' + str(tw) + '/FW=' + str(fw) + '/MENU=' + str(menu));
				switch_window( view_win );
				return_str = global_str("~TAG_VIEW_RO");
				if( parse_int("/BID=", return_str) == buffer_id ) {
					read_only = parse_int("/RO=", return_str );
				}
			}
			switch_window( view_win );
			mark_pos;
			if( link_stat ) {
				jx = 0;
				tl = buffer_id;
				while( jx++ < window_count ) {
					switch_window( jx );
					if( (jx != view_win) && (buffer_id == tl)) {
						break;
					}
				}
			}
			else {
				rm("CREATEWINDOW");
				link_window( view_win );
				rm("EXTSETUP");
			}
			jx = window_id;
			call cleanup;
			switch_win_id(jx);
			goto_mark;
			goto exit;
		}
		else if (return_int == 23 ) {
			int txw, tn;
			int x = 5, y = 4, w = 65, l = 14;
			int uo = parse_int("/WU=", global_str("METAGS_CONFIG"));
			refresh = FALSE;
			rm("AUTOSAVE");
			put_box( x, y, x + w, y + l, 0, m_b_color, 'SCANNING', TRUE );
			set_vp( x + 1, y + 1, x + w - 3, y + l - 2 );
			gotoxy_vp( 1, 1 );
			text_color_vp = m_s_color;
			switch_window( window_count );
			create_window;
			txw = cur_window;
			switch_window( fw );
			tof;
			while(!at_eof) {
				if( (check_key) && (key1 = 27)) {
					rm('userin^verify /T=Abort tag scanning?/H=metags^*');
					if( return_int ) {
						break;
					}
				}
				goto_col(1);
				return_str = get_word(" ");
				write_vp( return_str );
				p = parse_str("P=", get_line) + return_str;
				return_str = p;
				rm("SETFILENAME");
				p = return_str;
				if( !first_file( return_str ) ) {
					if( uo ) {
						rm('tag_find_file /F=1/FN=' + p);
						if( return_int ) {
							tn = parse_int('þDT=', get_line );
							if( last_file_time <= tn ) {
								write_vp("  ...file not changed");
								switch_window( tw );
								goto skip_build;
							}
						}
					}
					switch_window( txw );
					rm('ldfiles /NC=1/NHA=1');
					write_vp( '.' );
					if(error_level == 0) {
						rm('tag_build /WW=1' );
					}
					refresh = FALSE;
				}
			skip_build:
				error_level = 0;
				write_vp( ".\n\r" );
				switch_window( fw );
				down;
			}
			set_vp( 1, 1, screen_width, screen_length );
			kill_box;
			switch_window( txw );
			delete_window;
			call cleanup;
			return_int = TRUE;
			goto again;
		}
		else if (return_int == 21) {
				working;
        int menu_2 = menu_create;
				switch_window( window_count );
				create_window;
				ls = "DEFAULT";
				return_str = global_str('METAGS_FILE_NAME');
				call put_file_in_list;
				ls = "C/C++";
				return_str = global_str('TAG_F_C');
				call put_file_in_list;
				ls = "CMAC";
				return_str = global_str('TAG_F_CMAC');
				call put_file_in_list;
				ls = "MODULA_2";
				return_str = global_str('TAG_F_MODULA_2');
				call put_file_in_list;
				ls = "ASM";
				return_str = global_str('TAG_F_ASM');
				call put_file_in_list;
				ls = "PARADOX";
				return_str = global_str('TAG_F_PAL');
				call put_file_in_list;
				ls = "PARADOX";
				return_str = global_str('TAG_F_PARADOX');
				call put_file_in_list;
				ls = "DBASE";
				return_str = global_str('TAG_F_DBASE');
				call put_file_in_list;
				ls = "CLIPPER";
				return_str = global_str('TAG_F_CLIPPER');
				call put_file_in_list;
				ls = "XBASE";
				return_str = global_str('TAG_F_XBASE');
				call put_file_in_list;
				ls = "PASCAL";
				return_str = global_str('TAG_F_PASCAL');
				call put_file_in_list;
				tof;
        menu_set_item( menu_2, 1, "Tag files", "", "/C=1/DC=1/L=1/WIN=" + str(cur_window),
																	15, 0, 0 );
        return_int = menu_2;
				rm("userin^data_in /HN=1/T=Select tag file/#=1/H=metags^LIST");
				working;
        menu_delete( menu_2 );
				goto_col(1);
				forward_till(' |9|255');
				forward_till_not(' |9|255');
				return_str = get_word('');
				delete_window;
				call cleanup;
				if(return_int) {
					rm("tag_find_file /TF=" + return_str );
					if( return_int ) {
						goto again;
					}
				}
				goto again;
		}
		else
			call cleanup;
	}
	switch_win_id( ow );
	goto exit;

cleanup:
	while ( box_count > bc )
	{
		kill_box;
	}
	menu_delete( menu );
	switch_window( view_win );
	delete_window;
	switch_window( fw );
	delete_window;
	switch_window( tw );
	window_attr = 0x81;
	ret;

put_file_in_list:
	{
		if( return_str != "" ) {
			pad_str( ls, 20, " " );
			put_line( ls + return_str );
			down;
		}
		ret;
	}

exit:
  keyword_highlighting = tkh;
}

macro tag_list_display
/*-----------------09-18-91 12:51pm-----------------
 Used by "tag_list".
 --------------------------------------------------*/
	trans2
//  no_break
{
	int tw = parse_int('/TW=', mparm_str ),
			fw = parse_int('/FW=', mparm_str ),
			cw = cur_window,
			menu = parse_int('/MENU=', mparm_str ),
			vw = parse_int('/VW=', mparm_str ),
			tr = refresh,
			m = parse_int('/XM=', mparm_str), tx1,ty1,
			tundo = undo_stat,
			ee,
			jx
			;
	str tstr[20], ps[60], fp[80];

	refresh = false;
	undo_stat = false;

	switch( m ) {
		case 0 :
			switch_window( fw );
			if(check_key) {
				push_key(key1,key2);
				goto exit;
			}
			mark_pos;
			goto_col(1);
			tstr = get_word( ' |9|255' );
			jx = parse_int('|127L=', get_line );
			fp = parse_str('|127P=', get_line );
			forward_till_not( ' |9|255' );
			goto_mark;
			if( jx == 0 )
				eof;
			switch_window( tw );
			refresh = true;
			redraw;
			refresh = false;
			goto_col(1);
			write( '³', wherex - 1, wherey, 0, b_color );
      while( (c_row > 1) && (c_line > 1))
				up;
			goto_line( jx );
			eol;
		sloop:
			if (search_bwd( '%' + TG_Start_File + tstr + ' ', 0 )) {
				ps = parse_str("þPATH=", get_line );
				if((ps != fp) && (c_line > 1) ) {
					up;
					eol;
					goto sloop;
				}
			}
			down;
			refresh = true;
			redraw;
			write( '', wherex - 1, wherey, 0, l_color );
			pad_str( ps, 46, " " );
      make_message(ps);
			break;

		case 4 :
			if( cw == tw ) {
				if(check_key) {
					push_key(key1,key2);
					goto exit;
				}
				call set_file;
			}
			break;

		case 1 :
			if( cw == tw ) {
				del_line;
			}
			else if( cw == fw ) {
				goto_col(1);
				tstr = get_word( ' |9|255' );
				forward_till_not( ' |9|255' );
				ee = val( jx, get_word(' |9|255'));
				del_line;
				goto_col( 1 );
				switch_window( tw );
				goto_line( jx );
				if( ee != 0 )
					eof;
				eol;
				if( search_bwd( '%' + TG_Start_File + tstr + ' ', 0 ) ) {
					block_begin;
					eol;
					if(!search_fwd( '%' + TG_Start_File, 0 ) )
						eof;
					else
						up;
					block_end;
					delete_block;
					down;
				}
				goto_col(1);
			}
			break;

		case 2 :
			{
				call view_tag;
			}
			break;
		case 3 :
			{
				switch_window( tw );
        Set_Global_Int("~OBJ_TREE_MODE", (menu_item_int( menu, 12, 2 ) != 0) +
                    ((menu_item_int( menu, 13, 2) != 0) * 2));
        rm("objtree^tag_object_tree /BOX=1");
				if( return_int == 10 ) {
					switch_window( tw );
					mark_pos;
					tof;
					if( search_fwd("%" + make_literal(return_str) + "[ \t]",0 ) ) {
						pop_mark;
						call set_file;
						call view_tag;
					}
					else
						goto_mark;
				}
			}
			break;

	}
	switch_window( cw );
	goto exit;


view_tag:
	{

		switch_window( vw );
		fp = global_str("~TAG_VIEW_RO");
		if( parse_int("/BID=", fp) == buffer_id ) {
			read_only = parse_int("/RO=", fp );
		}
		switch_window( tw );
		goto_col(1);
		set_global_str('LAST_TAG_FIND', get_word(' |9|255') );
		rm('tag_goto /VW=' + str( vw ) +'/OW=' + str( cw ) + '/EB=0/BC=' + str(box_count + 1));
		if( error_level != 0 ) {
			rm('MEERROR');
			error_level = 0;
		}
		switch_window( vw );
		set_global_str( "~TAG_VIEW_RO", "/BID=" + str(buffer_id) + "/RO=" + str(read_only ) );
		read_only = TRUE;
		if( menu != 0 )
			menu_set_str( menu, parse_int("/VMN=", mparm_str), 1, 'View: ' + file_name );
		ret;
	}

set_file:
	{
		reg_exp_stat = TRUE;
		switch_window(tw);
		mark_pos;
		tx1 = wherex;
		ty1 = wherey;
		if (search_bwd( '%' + TG_Start_File, 0 )) {
			forward_till(" " );
			forward_till_not(" ");
			tstr = get_word(" ");
			ps = parse_str("þPATH=", get_line );
			switch_window( fw );
			goto_col(1);
			refresh = true;
			display_line;
			refresh = false;
			if( !((tstr == get_word(" ")) && (parse_str("P=", get_line) == ps))) {
				write( '³', wherex - 1, wherey, 0, b_color );
				mark_pos;
				tof;
				while( search_fwd("%" + make_literal(tstr) + " ", 0 ) ) {
					if( parse_str("P=", get_line) == ps ) {
						break;
					}
          eol;
				}
				jx = c_line;
				goto_mark;
        while(( jx < c_line))
					up;
        while( (jx > c_line))
					down;
				refresh = true;
				goto_col(1);
				redraw;
				write( '', wherex - 1, wherey, 0, l_color );
				refresh = false;
			}
			switch_window( tw );
		}
		goto_mark;
		gotoxy( tx1,ty1 );
		ret;
	}
exit:
	refresh = tr;
	undo_stat = tundo;
}

macro tag_build
/*---------------08-22-91 10:16am-------------------
 *	Determines the correct macro to run (to build tags)
 *  for the given file, based on the language type
 *  setup for its extension.
 --------------------------------------------------*/
	no_break
	trans2
{
	str ls[40] = tag_language;
	int tm = system_timer,
      tkh = keyword_highlighting,
			ww = parse_int( '/WW=', mparm_str ),
			src_win = cur_window,
			work_win,
			sl,
			tundo = undo_stat;

	refresh = false;
  keyword_highlighting = FALSE;
	undo_stat = false;
	reg_exp_stat = true;
	set_global_int("~OBJ_TREE_BUILD", 1 );

	mark_pos;
	tof;
	working;
	if( !ww )
		RM("MEERROR^MESSAGEBOX /NW=1/M=SCANNING " + truncate_path(file_name) + "...");
	rm( 'tag_find_file /C=1' + mparm_str );
	work_win = cur_window;
	switch_window( src_win );
	if( (ls == 'C') || (ls == 'C++') )
			rm( 'tag_b_c /TW=' + str(work_win) );
	else
	if( ls == 'CMAC' )
			rm( 'tag_b_cmac /TW=' + str(work_win) );
	else
	if( ls == 'PASCAL' )
			rm( 'tag_b_pascal /TW=' + str(work_win) );
	else
	if( ls == 'MODULA_2' )
			rm( 'tag_b_modula2 /TW=' + str(work_win) );
	else
	if( ls == 'ASM' )
			rm( 'tag_b_asm /TW=' + str(work_win) );
	else
	if( (ls == 'PARADOX') || (ls == 'PAL') )
			rm( 'tag_b_pal /TW=' + str(work_win) );
	else
  if ((ls == 'DBASE') ||
			(ls == 'XBASE') ||
			(ls == 'CLIPPER') ||
			(ls == 'EVOLVE') ||
			(ls == 'CLI.CLIPPER') ||
			(ls == 'DBA.DBASE') ||
			(ls == 'ARA.ARAGO') ||
			(ls == 'FXP.FOXPRO') ||
			(ls == 'FXW.FOXPRO') ||
			(ls == 'DBF.DBFAST'))
			rm( 'tag_b_xbase /TW=' + str(work_win) );
	else {
		if( ww )
			write_vp('...No language defined.');
	}
	switch_window( src_win );
	rm('tag_b_explicit /TW=' + str(work_win));

	if(parse_int("/SORT=",global_str("METAGS_CONFIG")))
	{
		switch_window( work_win );
		reg_exp_stat = true;
		eof;
		sl = c_line;
		search_bwd("%" + tg_start_file, 0 );
		qsort_lines( c_line + 1, sl, 1, 1, 2048,  0 );

		switch_window( src_win );
	}

	if( !ww )
		make_message( 'Tag scanning took ' + str( ((system_timer - tm) * 10) / 182 ) + ' seconds.' );
	if( !ww ) {
		kill_box;
	}
	goto_mark;
  keyword_highlighting = tkh;
	undo_stat = tundo;
}

/****************************Multi-Edit Macro********************************

 NAME:         tag_b_explicit

 DESCRIPTION:  Scans for explicit tags.  Must be called by TAG_BUILD.

 PARAMETERS:   /TW=int		Tag window

 RETURNS:

*****************************09-23-91 03:14pm*******************************/
macro tag_b_explicit
{
	int src_win = cur_window,
			work_win = parse_int( "/TW=", mparm_str );
	str tag_id[80],
			tstr[80];

	tof;
	reg_exp_stat = TRUE;
	while( search_fwd('|64|64METAGS', 0 ) ) {     //
		forward_till( ' |9|255' );
		forward_till_not( ' |9|255' );
		tag_id = get_word( ' |9|255' );
		first_word;
		tstr = get_word( '' );
		call process_tag;
		eol;
	}
	goto exit;

process_tag:
	mark_pos;
	first_word;
	tstr = get_word('');
	goto_mark;
	switch_window( work_win );
	goto_col(1);
	put_line( tag_id + "\t\t" + tstr );
	down;
	switch_window( src_win );
	ret;
exit:

}


macro tag_b_c trans2
/*-----------------09-17-91 03:25pm-----------------
 *    Must be called by TAG_BUILD.
 *	Generates tags for C and C++ files.  Tags the
 *  following:
 *
 * 			function
 *      template
 * 			struct
 *      union
 *      class
 *      #define (if "tag_c_defines" is TRUE)
 *      typedef
 *      enum
 *      global variables and types
 *      @METAG tag    (explicit tag)
 *
 *--------------------------------------------------*/
{


#DEFINE _key_string "{=1[=2==3;=4:=5(=6,=7"
#DEFINE _open_curly_brace  1
#DEFINE _open_square_brace 2
#DEFINE _equal_sign        3
#DEFINE _semi_colon				 4
#DEFINE _colon             5
#DEFINE _open_paren        6
#DEFINE _comma             7

#DEFINE _key_string2 "_CLASSDEF=50typedef=51extern=52static=53signed=54unsigned=55struct=100union=101enum=102class=103template=104inline=105volatile=56pascal=57"
#DEFINE _classdef 50
#DEFINE _typedef  51
#DEFINE _extern   52
#DEFINE _static   53
#DEFINE _signed   54
#DEFINE _unsigned 55
#DEFINE _volatile 56
#DEFINE _pascal   57
#DEFINE _struct 	100
#DEFINE _union  	101
#DEFINE _enum   	102
#DEFINE _class  	103
#DEFINE _template 104
#DEFINE _inline   105



	int
			src_win = cur_window,
			work_win = parse_int('/TW=', mparm_str),
			jx,
			brace_count = 1,
			parm_pos = 0, parm_line = 0,
			char_pushed = FALSE,
			macro_done = FALSE,
			t_reg_exp_stat = reg_exp_stat,
			t_ignore_case = ignore_case,
			bc = box_count,
			brace_count = 0,
			tte = tab_expand,
			mline,mcol,
			else_count = 0,
			parm_type, is_method,
      saved_obj_line,                        // fix for format problems
      last_line,                             // formatting stuff
			tag_c_defines = parse_int("/TD=", global_str("METAGS_CONFIG" ))
			;

	char
			Current_Mac_Char;

	str
			current_parm = "",
			member_of[40],
      object_str[40],                        // added [40]
      saved_obj[40],                         // added to get global vars
      keywords[40] = '',                     // added to accumulate keywords
      saved_keywords[40] = '',               // added to accumulate keywords
			base_str,
			operator_set[20] = ":%^<>!-=+/|*&",
			tag_id[80],
      tmp_tag_id[80],  // new line
			tag_str,
			fn[80] = truncate_path( file_name )
			;

	refresh = FALSE;

	tab_expand = TRUE;
	reg_exp_stat = true;
	ignore_case = true;
	current_parm = "";


	while( !at_eof ) {
		call parse_next_parm;
	}

	goto exit;


parse_next_parm:
	{
		call get_next_parm;
		if( xpos( current_parm, ';//{})', 1 ) ) {
			put_line_num( c_line );
			if( current_parm == '//' ) {
				eol;
			}
			else
			if( current_parm == '{' ) {
				left;
				call match;
				if (!return_int) {
					++brace_count;
				}
			}
		} else if( str_char(current_parm, 1) == '#' ) {
			base_str = "";
			if( (current_parm == "#define") && (tag_c_defines) ) {
        if ( tag_c_defines ) {                  //
				  object_str = current_parm;
				  call get_next_parm;
				  tag_id = current_parm;
				  call process_tag;
        }                                       //
        call process_to_eol;                    //
			} else if ( current_parm == "#else" ) {
				call match_else;
			}
			if (at_eof)																//TR This prevents getting
				RET;                                    //   hung in a loop
			call process_to_eol;
		}
		else
		{
			base_str = "";
			call classify_parm;
			switch ( parm_type )
			{
				case _classdef :
					call process_to_eol;
					break;

				case _struct :
				case _union :
				case _enum :
				case _class :
				case _template :
					object_str = current_parm;
				pstruct:
		      saved_obj = object_str;
          call get_search_str;                      // Search string for beginning of structure
          call get_next_parm;                       //
          if ( current_parm != '{' ) {              // If function has a type
            do {                                    //
              tag_id = current_parm;                // Set possible type id
              call get_next_parm;                   //  and get next parameter.
            }                                       //
            while ( !at_eof &&                      // Search to end of definition
					        	!(xpos(current_parm,"{[=;:",  1))); // TMJ

						if(current_parm == ";")   // Check for forward defined class,
																			// don't tag it
						{
							if ( parm_type == _class )
							{
								ret;
							}
						}
						if( current_parm == ":")
						{
							base_str = "\tBASE=";
							do
							{
								call get_next_parm;
								while(!at_eof && ((current_parm == "public") || (current_parm == "private")
								     || (current_parm == "protected") || (current_parm == "virtual")))
								{
									call get_next_parm;
								}
				      	base_str += current_parm;  // Set base
								call get_next_parm;
								if(current_parm == ",")
								{
									base_str += ",";
								}
								else
									break;
							}
							while(!at_eof  && (current_parm != "{"));

							base_str += "";

              call process_tag2;                    // Process structure type
							while( !at_eof && (current_parm != "{"))
							{
								call get_next_parm;
							}
						}
            else if ( current_parm == '{' ) {       // type is defined so use
              object_str += '_type';                //
              call process_tag2;                    // Process structure type
              object_str = saved_obj;               // Restore object string
            } else if ( current_parm == ';' ) {     // Check for simple declaration
              call process_tag2;                    // Process if true and we are
              ret;                                  //  done.
            } else if ((parm_type == _struct) &&
                  ( current_parm == '=' )) {       // Check for init struct
							call PROCESS_INIT_STRUCT;
							ret;
					 	} else if ((parm_type == _struct) &&
								( current_parm == '[' )) {              // Check for array of
                call process_tag2;                      // structures
                call match_bracket;                     //
                call get_next_parm;                     //
                if ( current_parm == '[' ) {            // Check for doubly
                  call match_bracket;                   //
                  call get_next_parm;                   //
                }                                       //
								ret;
						}
					}
                                                  // Here for typeless struct

					if ( parm_type == _enum )
					{
						call process_enum;
					}
					else
					{
            left;                                   // Find closing brace to
            call match;                             //  typeless structure.
					}
          call get_next_parm;                     // Get name of structure
          if ( current_parm == ';' ) {          // If type only, we are
            ret;                                //  done.
          }                                     //
          do {                                    //
            call get_search_str;                  // Search string for beginning of structure
            tag_id = current_parm;                // Set possible struct name
            call get_next_parm;                   //  and get next parameter.
						if(xpos(current_parm,"([=;,",1))
						{
							object_str = saved_obj;
              if ( current_parm == '[' ) {              // Check for array of
                object_str += '_[]';                    //  structures
                call match_bracket;                     //
                call get_next_parm;                     //
                if ( current_parm == '[' ) {            // Check for doubly
                  object_str += '[]';                   //  dimensioned array
                  call match_bracket;                   //
                  call get_next_parm;                   //
                }                                       //
                call process_tag2;                      // Process struct name
            //    call process_to_semi;                   // Process to end of struct
              } else if ( current_parm == '(' ) {       // Check for procedure returning struct

								/* This currently does not identify typedefs of procedures
								   returning struct, union, etc.  If this is a typedef then
								   the ; represents a valid definition, otherwise it is only
								   a procedure prototype and is not kept. */

                object_str += '_proc';                  //
                left;                                   // Match parens
                call match;                             //
                call get_next_parm;                     //
                if ( current_parm != ';' ) {            // Use if not proc definition
                  call process_tag2;                    //
                  while ( ! at_eof && (current_parm != '{') ) { //
                    call get_next_parm;                 // Process to end of proc
                  }                                     //
                  if( current_parm == '{') {            //
										left;
                    call match;                         //
                  }                                     //
                }                                       //

              } else if ((parm_type == _struct) &&
                    ( current_parm == '=' )) {       // Check for init struct
								call PROCESS_INIT_STRUCT;
              } else {
                call process_tag2;                      // Process struct name
       //         call process_to_semi;                   // Process to end of struct
              }                                         //
						}
          }                                       //
					while ( !at_eof && !(current_parm == ";"));

					break;

				case _typedef :
					{
						object_str = current_parm;
						call get_next_parm;
						call  classify_parm;
						if( ( parm_type == _struct ) ||
								( parm_type == _union ) ||
								( parm_type == _enum ) ||
								( parm_type == _class )
								) {
			        object_str += '_' + current_parm;
						  goto pstruct;
						}
            while ( ! at_eof &&                       // added to allow typedefs
                    (current_parm != ';') ) {         // of arrays and
              if ( current_parm == '[' ) {            //
                call match_bracket;                   //
                call get_next_parm;                   //
                if ( current_parm == '[' ) {          //
                  call match_bracket;                 //
                  call get_next_parm;                 //
                }                                     //
              } else if ( ! xpos(current_parm, '()', 1) ) { // procedures and
                tag_id = current_parm;                //
                call get_next_parm;                   //
              } else if ( current_parm == '(' ) {     //
                while ( ! at_eof &&                   //
                        (current_parm != ')') ) {     //
                  tmp_tag_id = current_parm;          //
                  call get_next_parm;                 //
                }                                     //
                call get_next_parm;                   //
                if ( current_parm == '(' ) {          // procedure pointer
                  tag_id = tmp_tag_id;                //
                  left;                               //
                  call match;                         //
                  call get_next_parm;                 //
                }                                     //
              } else {                                //
                call get_next_parm;                   //
              }                                       //
            }                                         //

            call process_tag;                         // The following code allows
         //   call process_to_semi;                   //  for processing of global
					}
					break;

			  case _extern :
					{
            call get_next_parm;                       //  procedures. An additional
						if( copy(current_parm,1,1) == '"')
						{
							call get_next_parm;
						}
            if ( current_parm == '{' ) {              //  adjustment was made for
              call skip_block;                    //  procedures returning a
            } else {                                  //  procedure pointer.
              call process_to_semi;                   // The check for '{' if extern
            }                                         //  is because of name mangling
					}
					break;

				case _static :   // stuff in Borland C
            keywords += '_static';          // Setup up keyword prefix
						break;

				case  _signed :
            keywords += '_signed';          //  signed keyword
						break;

				case _inline :
						keywords += '_inline';
						break;

				case _unsigned :
            keywords += '_unsigned';        //  unsigned keyword.
						break;

				case _pascal :
						keywords += '_pascal';
						break;

				case _volatile :
						keywords += '_volatile';
						break;

				default:
						is_method = false;
                                                      // We have the beginning of
            saved_obj = current_parm + keywords;      //  a declaration at this point.
            saved_obj_line = c_line;                  // When '-+=([{;,' is not object line.
            while( !at_eof &&                         // Outside while loop is for
                   (current_parm != ';') ) {          //  multiple declarations using ','.
              tag_id = current_parm;                  // We are doing a bunch of
              call get_next_parm;                     //  hocus pocus here because
              if ( (current_parm == ';') ||           //  someone can declare an
                   (current_parm == ',') ) {          //  unsigned or signed int
                if ( str_char(keywords, 1) == '_' ) { //  without including 'int'.
                  saved_obj = 'int' + keywords;       // Keywords will start with
                } else {                              //  '_' if no data type
                  saved_obj = keywords;               //  was declared in the
                }                                     //  statement.
              }                                       //

              while( !at_eof &&                       // Pass up all the rest
                    !xpos(current_parm, ':-+=([{;,', 1)) { // while keeping
								if(current_parm == "::")
								{
									is_method = TRUE;
									member_of = tag_id;
								}
								else if( current_parm == "operator")
								{
									if( is_method )
									{
										call get_next_parm;
										tag_id = member_of + "::" + current_parm;
										object_str = "operator_method";
										call process_tag;
										call skip_block;
										ret;
									}
								}
                tag_id = current_parm;                //  tag ID one parameter
                call get_next_parm;                   //  behind.
              }                                       //
							int parm_type2 = parse_int(""+current_parm+"=",_key_string);

							call get_search_str;
              if( parm_type2 == _open_paren ) {            //
                saved_obj = saved_obj + '_proc';      //
                mark_pos;                             //
                goto_line(saved_obj_line);            //
                first_word;                           //
                tag_str = get_word('');               //
                goto_mark;                            //
								left;
			          call match;                           //TMJ 12-04-92 10:43am
                call get_next_parm;                   //
                if ( current_parm == '(' ) {          // returning procedure ptr
                  saved_obj = saved_obj + '_*proc';   //
                  tag_id = tmp_tag_id;                //
                  left;                               //
                  call match;                         //
                  call get_next_parm;                 //
                }                                     //
                if( current_parm != ';' ) {           //
                  object_str = saved_obj;             //
                  call process_tag2;                  //
									call skip_block;
                } else {                              //
                  keywords = '';                      //
                }                                     //
                break;                                //


              } else if (parm_type2 == _open_square_brace) {       // global variables
                object_str = saved_obj + '_[]';       //
                call match_bracket;                   //
                call get_next_parm;                   //
                if (current_parm == '[') {            //
                  object_str = object_str + '[]';     //
                  call match_bracket;                 //
                  call get_next_parm;                 //
                }                                     //
                if (current_parm == ',') {            //
                  saved_keywords = object_str;        //
                  call process_tag;                   //
                  keywords = saved_keywords;          //
                } else if ( current_parm == ';') {    //
                  call process_tag;                   //
                  break;                              //
                } else if ( current_parm == '=') {    // initialized array
                  call process_tag;                   //
                  call process_to_semi;               //
             //     break;                              //
                }                                     //


              } else if (parm_type2 == _open_curly_brace) {       //
                left;                                 //
                call match;                           //
                break;                                //


              } else if (parm_type2 == _semi_colon) {       //
                object_str = saved_obj;               //
                call process_tag;                     //
                break;                                //


              } else if (parm_type2 == _equal_sign) {       // initialized variables
                object_str = saved_obj;               //
                call process_tag;                     //
                call process_to_semi;                 //

              } else if (parm_type2 == _colon) {       // initialized variables
                object_str = saved_obj;               //
                call process_tag;                     //
                call process_to_semi;                 //
             //   break;                                //

              } else if (parm_type2 == _comma) {       // multiple declarations
                object_str = saved_obj;               //
                saved_keywords = object_str;          //
                call process_tag;                     //
                keywords = saved_keywords;            //
              }                                       //
              if(current_parm != ";")
                call get_next_parm;                   //
            }                                         //

            keywords = "";
			}
    }                                           //
    ret;                                        //
	}

PROCESS_INIT_STRUCT:
	{
		call process_tag2;                   // Process struct name
		call get_next_parm;                  // move past init code
		if( current_parm == '{') {
		  left;
		  call match;
		  call get_next_parm;
		}
	}


skip_block:
	while ( !at_eof && (current_parm != '{') ) {
		call get_next_parm;
	}
	if( current_parm == '{') {
		left;
		call match;
	}
	ret;
                                              //
match_else:
	{
		eol;
		++else_count;
		while( !at_eof && else_count ) {
			call get_next_parm;
			if( current_parm == "#ifdef" ) {
				++else_count;
			} else if ( current_parm == "#ifndef" ) {
				++else_count;
      } else if ( current_parm == "#if" ) {      // make usable for
        call get_next_parm;                      // "#if defined()" and
        if( (current_parm == "defined") ||       // "#if !defined()" and
            (current_parm == "!defined") ) {     // "#if ! defined()"
          ++else_count;                          //
        } else if ( current_parm == "!" ) {      //
          call get_next_parm;                    //
          if ( current_parm == "defined") {      //
            ++else_count;                        //
          }                                      //
        }                                        //
			}  else if(current_parm == "#endif" ) {
				--else_count;
			}
		}
		ret;
	}

process_to_eol:
  {                                             // Process to end of line
    last_line = c_line;                         //  insuring that multiline
    do {                                        //  comments and line
      if ( current_parm == '/*' ) {             //  extenders allow the
        call do_comment;                        //  line to extend beyond
        last_line = c_line;                     //  an eol character
      }                                         //
      call get_next_parm2;                      //
    } while ( !at_eof && (last_line == c_line) ); //
    goto_line(last_line);                       //
    eol;                                        //
    left;                                       //
    if ( cur_char == '\' ) {                    //
      call next_line;                           //
      goto process_to_eol;                      //
    } else {                                    //
      eol;                                      //
    }                                           //
    ret;                                        //
  }                                             //

process_to_semi:
	{
		while( !at_eof && (current_parm != ";") && (current_parm != ",")) {
			if( current_parm == "{" ) {
					left;
					call match;
			}
			else if(current_parm == "(")
			{
				left;
				call match;
			}
			else if(current_parm  == "[")
			{
				call match_bracket;
			}
			call get_next_parm;
		}
		ret;
	}

process_enum:
	do
	{
		call get_next_parm;
		if( current_parm != "}")
		{
			tag_id = current_parm;
			object_str = "enum_item";
			call process_tag2;
			object_str = saved_obj;
			do
			{
				call get_next_parm;
			}
			while(!at_eof && !((current_parm == ",") || (current_parm == "}")));
		}
	} while(!at_eof && (current_parm != "}"));
	ret;

classify_parm:
	parm_type = parse_int( "" + current_parm + "=", _key_string2 );
	ret;

get_search_str:
	{
		mark_pos;
		first_word;
		tag_str = copy( get_line, c_col, 128 );
		goto_mark;
		ret;
	}

process_tag:
	{
		call get_search_str;
process_tag2:
		switch_window( work_win );
		goto_col(1);
		put_line( tag_id + "\t"+ object_str + base_str + "\t" + tag_str );
		down;
		switch_window( src_win );
    keywords = '';                     // added keyword support
		ret;
	}

do_comment:
	if (search_fwd( '{@*|47}||{|47@*}', 0)) {
		if( found_str == '|47*' ) {
			right;
			right;
			call do_comment;
		}
	}
	right;
	right;
	ret;

match_bracket:
	while ((current_parm != ']') && !at_eof) {
			call get_next_parm;
			if (current_parm == '[') {
				call match_bracket;
				call get_next_parm;
			}
	}
	ret;

Mac_Get_Next_Char:
	{
		again:
			If (at_eol)
				{
					Current_Mac_Char = ' ';
					call next_line;
					forward_till_not(' |9|255');
					ret;
				}
			Current_Mac_Char = cur_char;
			right;
		ret;
	}


get_next_parm:
	{
		mline = c_line;
		mcol = c_col;
		call get_next_parm2;
		if( current_parm == '|47*' ) {
			call do_comment;
			goto get_next_parm;
		}
		ret;
	}


get_next_parm2:
		{
			int end_quote;
			char x_char;
parm_again:
			Current_Parm = '';
			forward_till_not( ' |9|255|12');
			while (at_eol) {
				call next_line;
				forward_till_not( ' |9|255|12');
				if( at_eof )
					ret;
			}
			current_mac_char = cur_char;
			parm_pos = c_col;
			right;
			parm_line = c_line;
			if( xpos(current_mac_char, '{}()[];,',1)) {
				current_parm = current_mac_char;
				ret;
			}

			if(  current_mac_char == '/'  ) {
				current_parm = current_mac_char;
				call mac_get_next_char;
				if(current_mac_char == '/')
				{
					eol;
					goto parm_again;
				}
				if(  (current_mac_char == '*') ||
					 (current_mac_char == '=') ) {
					 current_parm = current_parm + current_mac_char;
				} else
					left;
				ret;
			}
			if(  Current_Mac_Char == '*'  ) {
				current_parm = current_mac_char;
				call mac_get_next_char;
				if(  (current_mac_char == '=') ||
					 (current_mac_char == '/')  ) {
					 current_parm = current_parm + current_mac_char;
				} else
					left;
				ret;
			}

			if( xpos(Current_Mac_char, Operator_Set, 1))
				{
					do {
						Current_Parm += Current_Mac_Char;
						if(  (current_parm == '{') || (current_parm == '}'))
							ret;
						call mac_get_next_char;
					} while ( (xpos(current_mac_char, Operator_Set, 1)) && !at_eof );
					left;
					ret;
				}


			if(  Current_Mac_Char == ''''  )
				{
					end_quote = false;
					do {
						forward_till('''\');
						if (at_eol) {
							break;
						}
						else if(  Cur_Char == ''''  )
							{
								right;
								break;
							}
							else if(  Cur_Char == '\'  ) {
								right;
								right;
							}
					} while( !end_quote && !at_eof );
					if (!at_eof)
							goto parm_again;
				}
			else
			if(  Current_Mac_Char == '"'  )
				{
					end_quote = false;
					do {
						forward_till('"\');
						if (at_eol) {
							break;
						}
						else if(  Cur_Char == '"'  )
							{
								right;
								break;
							}
						else if( cur_char == '\' ) {
							right;
							right;
						}
					} while( !at_eof);
					if (!at_eof)
							goto parm_again;
				}
			else
				{
					current_parm = current_mac_char + get_word('|9{}, )([];|0|255|12''' + operator_set);
				}
			ret;
		}


next_line:
	{
		down;
		goto_col(1);
		ret;
	}



match: {
	str  Str1[20],Str2[20],  Search_Str ;  /* Strings to match */

	int  Direction,    /* 1 = Search forward, 0 = backward */
					 B_Count,      /* Brace count.  0 = match found */
					 S_Res,        /* Result of last search */
					 pre_count,
					 JX;           /* General purpos */

Find_Match_Str:
		if( current_parm == '(' ) {
			str1 = '(';
			str2 = ')';
		} else {
			Str1 = '{';
			Str2 = '}';
		}
Start_Match:

	Search_Str = '[#"''/' + Str1 + Str2 + ']';

	Reg_Exp_Stat = True;
	Ignore_Case = True;					/* Ignore the search case */
	B_Count = 1;								/* Brace count starts at 1 */
	S_Res = 1;									/* Init search result to true */
	pre_count = 0;
															/* Tell the user what we're matching */

MATCH_LOOP:     /* Main loop */

		if(  S_Res == 0  ) {           /* If the last search was a failure then exit */
			Goto Error_Exit;
		}

		if(  B_Count == 0  ) {         /* If brace count is zero then success */
			Goto Found_Exit;
		}

			/*   We are going to search not only for the match brace or paren, but
				 also for comments and double and single quotes */
		Right;
		while(  (NOT (At_EOL)) & ((Cur_Char == '|255') |
					(Cur_Char == '|9'))  ) {
			RIGHT;
		}
		S_Res = Search_Fwd( Search_Str, 0);

										/* If the search result was a failure then exit */
	if(  S_Res == 0  ) {
		Goto Error_Exit;
	}

	if( found_str == "#" ) {
		mark_pos;
		first_word;
		if( cur_char == "#" ) {
			pop_mark;
			found_str = get_word(" \t" );
			if(( found_str == "#ifdef" ) || (found_str == "#ifndef") ) {
				if( pre_count > 0 ) {
					++pre_count;
				}
			}
			else if (found_str == "#else" ) {
				if( pre_count == 0 )
					++pre_count;
			}
			else if( found_str == "#endif" ) {
				if( pre_count > 0 ) {
					--pre_count;
				}
			}
			eol;
		}
		else
			goto_mark;
		goto match_loop;
	}


										/* If we found the original string then up the count */
	if(  Found_Str == STR1  ) {
		if( pre_count == 0 )
			B_Count = B_Count + 1;
		Goto Match_Loop;
	}
										/* If we found the matching string then decrement the count */
	if(  Found_Str == STR2  ) {
		if( pre_count == 0 )
			B_Count = B_Count - 1;
		Goto Match_Loop;
	}

										/* If we found a single quote the match it  */
	if(  Found_Str == ''''  ) {
		Quote_Loop:
			RIGHT;
			S_Res = Search_Fwd('[\'']',0);
			if(  S_Res == 0  ) {
				Goto Error_Exit;
			}
			if(  Found_Str == '\'  ) {
				right;
				Goto Quote_Loop;
			}
			Goto Match_Loop;
	}

											/* If we found a double quote then match it */
	if(  Found_Str == '"'  ) {
		Quote_Loop2:
				RIGHT;
				S_Res = Search_Fwd('[\"]',0);
			if(  S_Res == 0  ) {
				Goto Error_Exit;
			}
			if(  Found_Str == '\'  ) {
				right;
				Goto Quote_Loop2;
			}
			Goto Match_Loop;
	}

											/* If we found a opening comment then match it */
	if(  (Found_Str == '/')  ) {
			Right;
			if(  (Cur_Char == '*')  ) {
				S_Res = Search_Fwd('@*/',0);
        right;                          // second right is in loop above
			} else if( cur_char == '/' ) {		// Handle possibility of C++ comments
				eol;
			} else {
				left;
			}
			Goto Match_Loop;
	}



Error_Exit:       /* We go here if no match was found */
	return_int = FALSE;
	Goto Macro_Exit;

Found_Exit:       /* We go here if a match was found */
	right;
	return_int = TRUE;
Macro_Exit:
	ret;
}


exit:
	reg_exp_stat = t_reg_exp_stat;
	ignore_case = t_ignore_case;
	tab_expand = tte;
}

/*-----------------09-18-91 12:49pm-----------------
 Scans CMAC files for tags.    Must be called by TAG_BUILD.
 --------------------------------------------------*/
macro tag_b_cmac trans2 {

	int				work_win = parse_int( "/TW=", mparm_str ),
						src_win = cur_window,
						ttm = tab_expand,
						jx
						;

	str				tstr,
						fn[128] = truncate_path( file_name ),
						tag_id[100], object[40]
						;

	refresh = false;
	tab_expand = TRUE;

	reg_exp_stat = TRUE;
	ignore_case = TRUE;

	tof;

 again:
	while( search_fwd( '{global}||{prototype}||{macro}||{void}||{int}||{str}||{real}[ |9]', 0 ) ) {
		put_line_num( c_line );
		jx = c_col;
		first_word;
		if( jx == c_col ) {
			object = caps(get_word(' |9|255'));
			if((object == "GLOBAL") || (object == "PROTOTYPE"))
			{
				search_fwd("@{", 0);
				search_fwd("@}", 0);
				goto again;
			}
			forward_till_not( ' |9|255' );
			tag_id = get_word( ' |9|255{(' );
			forward_till_not(' |9|255');
			if(object != "MACRO")
			{
				if((cur_char == ";") || (cur_char == ","))
					goto again;
			}
			first_word;
			tstr = copy( get_line, c_col, 128 );
		sagain:
			if( search_fwd( "[@{@};@*]", 0 ) ) {
				if( found_str == "*" ) {
					right;
					if( cur_char != "/" ) {
						left;
						if(c_col == 1) {
							right;
							goto sagain;
						}
						left;
						if(cur_char != "/") {
							right;
							right;
							goto sagain;
						}
						if( search_fwd( '@*/', 0 ) ) {
							right;
							right;
							goto sagain;
						}
					}
					right;
				}
				else if( found_str == "{" ) {
          mark_pos;
					eol;
					call process_tag;
          goto_mark;
          rm( 'C^CMTCH ' + mparm_str );
				}
			}
		}
		else
			eol;
	}
	switch_window( src_win );

	goto exit;

process_tag:
	switch_window( work_win );
	goto_col(1);
	put_line( tag_id + "\t\t" + tstr );
	down;
	switch_window( src_win );
	ret;

exit:
	tab_expand = ttm;
}

/*-----------------09-18-91 01:26pm-----------------
 Build tags for Turbo-Pascal files.     Must be called by TAG_BUILD.
 --------------------------------------------------*/
macro tag_b_pascal trans2 {

	int				work_win = parse_int('/TW=', mparm_str),
						src_win = cur_window,
						scan_imp = parse_int('/PI=', global_str('METAGS_CONFIG')),
						ttm = tab_expand,
						unit_line = 0,
						interface_line = 0,
						implementation_line = 0,
						jx
						;

	str       sstr,
						tstr,
						object_str[128],
						current_parm[128],
						fn[128] = truncate_path( file_name ),
						tag_id[100],
						seperators[40] = ' |9|255~!@#$%^{}()&*()+=||"'':;<>?,/~`[]'
						;

	refresh = false;
	tab_expand = TRUE;

	reg_exp_stat = TRUE;
	ignore_case = TRUE;

	tof;
	mark_pos;
	tstr = "{unit}|{program}";
	call do_search;
	if( found_str == "unit" ) {
		object_str = found_str;
		unit_line = c_line;
		call get_next_object;
		tag_id = current_parm;
		call process_tag;
		pop_mark;
		sstr = "{record}|{object}|{implementation}";
	} else {
		sstr = "{record}|{object}|{procedure}|{function}|{implementation}|{constructor}|{destructor}";
		goto_mark;
	}

	do
	{
		tstr = sstr;
		call do_search;
		object_str = found_str;
		if( found_str == "object" ) {
      object_str += "\tBASE=";
			mark_pos;
			call get_next_object;
			if( current_parm == "(" ) {
				call get_next_object;
        object_str += current_parm;
			}
			object_str += " ";
			goto_mark;
			goto do_record;
		}
		else if(found_str == "record") {
		do_record:
			mark_pos;
/* this is the old way that did not take into account that the identifier
 and/or the = sign might be on a line above the keyword
			first_word;
*/
			tstr = "=";
			call SCAN_BACK;
			if (svl(tag_id))
				goto UNKNOWN_RECORD;

			tstr = "a-z";
			call SCAN_BACK;
			if (svl(tag_id))
				goto UNKNOWN_RECORD;

			word_left;

			call get_next_object;
			tag_id = current_parm;
UNKNOWN_RECORD:
			call process_tag;
			goto_mark;
			forward_till( seperators );
		}
		else if ((found_str == "procedure") || (found_str == "function") ||
			(found_str == "constructor") || (found_str == "destructor") )
		{
			call get_next_object;
			if( current_parm != ';' ) {
				tag_id = current_parm;
				call process_tag;
			}
		}
		else if( found_str == "implementation" ) {
			if(!scan_imp)
				break;
	 		sstr = "{record}|{object}|{procedure}|{function}|{constructor}|{destructor}";
		} else
			break;
	} while ( !at_eof );



	goto exit;

SCAN_BACK:
	do { // find desired stuff while skipping over any comments
		if (!search_bwd("[" + tstr + "})]",0))
			goto BAD_SCAN;
		if (found_str == "}") { // {} comment, skip over it
			if (!search_bwd("@{",0))
				goto BAD_SCAN;
		} else if (found_str == ")") { // (**) comment, skip over it
			if (!search_bwd("(",0))
				goto BAD_SCAN;
		}
	} while (xpos(found_str,"{}()",1));
	tag_id = "";
	RET;

BAD_SCAN:
	tag_id = "UNKNOWN";
	RET;

process_tag:
	mark_pos;
	first_word;
	tstr = get_word('');
	goto_mark;
	switch_window( work_win );
	goto_col(1);
	put_line( tag_id + "\t" + object_str + "\t" + tstr );
	down;
	switch_window( src_win );
	ret;

do_search:
	{
		do {
			if( search_fwd( tstr + "|[@{@']|{(@*}", 0 ) ) {
				if( found_str == "(*" ) {
					search_fwd( "@*)",0 );
					continue;
				}
				if( found_str == "{" ) {
					search_fwd( "@}", 0 );
					continue;
				}
				if( found_str == "'") {
					right;
					search_fwd( "'", 1 );
					continue;
				}
				if( c_col > 1 ) {
					left;
					if( !xpos(cur_char, seperators, 1 ) ) {
						right;
						right;
						continue;
					}
					right;
				}
				goto_col( c_col + length( found_str ) );
				if( xpos(cur_char, seperators, 1 ) ) {
					found_str = lower( found_str );
					break;
				}
			}
			else {
				found_str = "";
				break;
			}
		} while( !at_eof || error_level );
		put_line_num( c_line );
		ret;
	}

get_next_object:
	{
		do {
			while( at_eol && !at_eof ) {
				down;
				first_word;
			}
			forward_till_not( '|9|255 ' );
			if( at_eol )
				continue;
			if(cur_char == '{') {
				search_fwd('@}',0);
				continue;
			}
			if(cur_char == '(') {
				right;
				if(cur_char == '*') {
					search_fwd('*)',0);
					continue;
				}
				else {
					left;
					current_parm = cur_char;
					right;
					break;
				}
			}
			if( xpos(cur_char, '+=-);*^[]$%@~!&<>,?/', 1 )) {
				current_parm = cur_char;
				right;
				break;
			}
			current_parm = get_word(seperators);
			break;
		} while( !at_eof );

		ret;
	}

exit:
	tab_expand = ttm;
}


/*-----------------09-18-91 01:26pm-----------------
 Build tags for Modula-2 files.     Must be called by TAG_BUILD.
 --------------------------------------------------*/
macro tag_b_modula2 trans2 {

	int				work_win = parse_int('/TW=', mparm_str),
						src_win = cur_window,
						ttm = tab_expand,
						unit_line = 0,
						interface_line = 0,
						implementation_line = 0,
						jx
						;

	str				tstr,
						object_str[30],
						current_parm[128],
						fn[128] = truncate_path( file_name ),
						tag_id[100],
						seperators[40] = ' |9|255~!@#$%^{}()&*()+=||"'':;<>?,./~`[]'
						;

	refresh = false;
	tab_expand = TRUE;

	reg_exp_stat = TRUE;
	ignore_case = FALSE;

	tof;

	do
	{
		tstr = "{RECORD}|{PROCEDURE}";
		call do_search;
		object_str = found_str;
		if(found_str == "RECORD") {
		do_record:
			mark_pos;
			first_word;
			call get_next_object;
			tag_id = current_parm;
			call process_tag;
			goto_mark;
			forward_till( seperators );
		}
		else if (found_str == "PROCEDURE" )
		{
			call get_next_object;
			if( current_parm != ';' ) {
				tag_id = current_parm;
				call process_tag;
			}
		} else
			break;
	} while ( !at_eof );

	goto exit;

process_tag:
	mark_pos;
	first_word;
	tstr = get_word('');
	goto_mark;
	switch_window( work_win );
	goto_col(1);
	put_line( tag_id + "\t" + object_str + "\t" + tstr );
	down;
	switch_window( src_win );
	ret;

do_search:
	{
		do {
			if( search_fwd( tstr + "|{(@*}", 0 ) ) {
				if( found_str == "(*" ) {
					search_fwd( "@*)",0 );
					continue;
				}
				if( found_str == "'") {
					right;
					search_fwd( "'", 1 );
					continue;
				}
				if( c_col > 1 ) {
					left;
					if( !xpos(cur_char, seperators, 1 ) ) {
						right;
						right;
						continue;
					}
					right;
				}
				goto_col( c_col + length( found_str ) );
				if( xpos(cur_char, seperators, 1 ) ) {
					break;
				}
			}
			else {
				found_str = "";
				break;
			}
		} while( !at_eof || error_level );
		put_line_num( c_line );
		ret;
	}

get_next_object:
	{
		do {
			while( at_eol && !at_eof ) {
				down;
				first_word;
			}
			forward_till_not( '|9|255 ' );
			if( at_eol )
				continue;
			if(cur_char == '{') {
				search_fwd('@}',0);
				continue;
			}
			if(cur_char == '(') {
				right;
				if(cur_char == '*') {
					search_fwd('*)',0);
					continue;
				}
				else {
					left;
					current_parm = cur_char;
					right;
					break;
				}
			}
			if( xpos(cur_char, '+=-);*^[]$%@~!&<>.,?/', 1 )) {
				current_parm = cur_char;
				right;
				break;
			}
			current_parm = get_word(seperators);
			break;
		} while( !at_eof );

		ret;
	}

exit:
	tab_expand = ttm;
}


/****************************Multi-Edit Macro********************************

 NAME:         tag_b_pal

 DESCRIPTION:  Builds tags for Paradox.     Must be called by TAG_BUILD.

 PARAMETERS:

 RETURNS:

*****************************09-23-91 03:28pm*******************************/
macro tag_b_pal
		trans2
{

	int				work_win = parse_int( "/TW=", mparm_str ),
						src_win = cur_window,
						ttm = tab_expand,
						jx
						;

	str				tstr,
						fn[128] = truncate_path( file_name ),
						tag_id[100]
						;

	refresh = false;
	tab_expand = TRUE;

	reg_exp_stat = TRUE;
	ignore_case = TRUE;

	tof;

	while( search_fwd( 'proc[ |9]', 0 ) ) {
		put_line_num( c_line );
		jx = c_col;
		first_word;
		if( jx == c_col ) {
			forward_till( ' |9|255' );
			forward_till_not( ' |9|255' );
			tag_id = get_word( ' |9|255(' );
			if( caps(tag_id) == 'CLOSED' ) {
				word_right;
				tag_id = get_word( ' |9|255(' );
			}
			first_word;
			tstr = get_word('');
			call process_tag;
		}
		else
			eol;
	}
	switch_window( src_win );

	goto exit;

process_tag:
	switch_window( work_win );
	goto_col(1);
	put_line( tag_id + "\t\t" + tstr  );
	down;
	switch_window( src_win );
	ret;

exit:
	tab_expand = ttm;
}

/****************************Multi-Edit Macro********************************

 NAME:         tag_b_asm

 DESCRIPTION:  Builds tags for ASM.     Must be called by TAG_BUILD.

 PARAMETERS:

 RETURNS:

*****************************09-23-91 03:28pm*******************************/
macro tag_b_asm
		trans2
{

	int				work_win = parse_int( "/TW=", mparm_str ),
						src_win = cur_window,
						ttm = tab_expand,
						jx
						;

	str				tstr,
            object_str[40],     // added to hold object string
						fn[128] = truncate_path( file_name ),
						tag_id[100]
						;

	refresh = false;
	tab_expand = TRUE;

	reg_exp_stat = TRUE;
	ignore_case = TRUE;

	tof;

  while( search_fwd( "{PROC}|{STRUC}|{RECORD}|{UNION}|{LABEL}|{MACRO}|{EQU}|{DB}|{DW}|{DD}[ \t;]|$", 0 ) ) { // added equ, db, dw, dd
		if( c_col != 1 ) {
			left;
			if(!xpos(cur_char,' |9|255',1) ) {
				right;
				right;
				continue;
			}
			right;
		}
		jx = c_col;
		if(search_bwd(';', 1)) {
			eol;
			continue;
		}
		goto_col(c_col);
		put_line_num( c_line );
    object_str = caps(get_word(' |9|255;')); // changed tag_id to object_str
		first_word;
		if( jx != c_col ) {   //if object_str and first word are not the same
			tag_id = get_word(' |9|255');  //then first word is tag_id
		}
		else
		{
      if ( (object_str == 'DB') ||       // ignore blank tag id's if
           (object_str == 'DW') ||       // tag type is db, dw or dd
           (object_str == 'DD') ) {      //
        tag_id = '';                     //
      } else {                           // otherwise tag_id is next word
			  word_right;
			  tag_id = get_word(' |9|255;');
      }                                  //
		}
		first_word;
		tstr = get_word('');
		eol;
    if ( tag_id != '' ) {                // process only valid tag id's
      call process_tag;
    }                                    //
	}
	switch_window( src_win );

	goto exit;

process_tag:
	switch_window( work_win );
	goto_col(1);
  put_line( tag_id + "\t" + object_str + "\t" + tstr ); // include tag type
//  put_line( tag_id + "\t\t" + tstr  ); // handled in above line
	down;
	switch_window( src_win );
	ret;

exit:
	tab_expand = ttm;
}

/****************************Multi-Edit Macro********************************

 NAME:         tag_b_xbase

 DESCRIPTION:  Builds tags for xBase.     Must be called by TAG_BUILD.

 PARAMETERS:

 RETURNS:

*****************************09-23-91 03:28pm*******************************/
macro tag_b_xbase
		trans2
{

	int				work_win = parse_int( "/TW=", mparm_str ),
						src_win = cur_window,
						ttm = tab_expand,
						jx, jy
						;

	str				tstr,
						fn[128] = truncate_path( file_name ),
						tag_id[100]
						;

	refresh = false;
	tab_expand = TRUE;

	reg_exp_stat = TRUE;
	ignore_case = TRUE;

	tof;

	while( search_fwd( "{PROC}|{FUNC}|{METHOD}", 0 ) ) {
		jx = c_col;
		first_word;
		if( caps(get_word(' |9|255{')) == "STATIC") {
			word_right;
		}
		else
				first_word;
		if( jx == c_col ) {
			tstr = " " + caps(get_word(' |9|255{')) + " ";
			if( jy = xpos( tstr, " PROC FUNC PROCEDURE FUNCTION METHOD ", 1) ) {
				put_line_num( c_line );
				word_right;
		 //	if( jy == 2 ) {
		 //		word_right;
		 //	}
				tag_id = get_word(' |9|255(');
				first_word;
				tstr = get_word('');
				eol;
				call process_tag;
			}
		}
		else {
			goto_col(jx);
			right;
		}
	}
	switch_window( src_win );

	goto exit;

process_tag:
	switch_window( work_win );
	goto_col(1);
	put_line( tag_id + "\t\t" + tstr  );
	down;
	switch_window( src_win );
	ret;

exit:
	tab_expand = ttm;
}

macro tag_scan_files
/*-----------------09-18-91 12:44pm-----------------
 Tag scans multiple files via dos wildcard.
 Parameters:
		/F=str		Wildcard file spec.  If /F= is not
							passed, then the user is prompted
							for the wildcard spec.
 --------------------------------------------------*/
		trans2
		no_break
{
	int jx,
			ow = window_id,
			tw,
			scan_level = 0,
			do_break = 0,
			t_message_row = message_row,
			x = 5, y = 4, w = 65, l = 14,
			t_search_attr = file_search_attr,
			uo = parse_int("/WU=", global_str("METAGS_CONFIG")),
			sd = global_int("TAG_SCAN_DIRS");

	str fstr[80],
      search_path[132];            // solve subdirectory problems


	refresh = false;
	message_row = 0;
	set_global_int("MENU_LEVEL", global_int("MENU_LEVEL") + 1 );
	switch_window( window_count );
	create_window;
	tw = cur_window;
	return_str = parse_str('/F=', mparm_str );
	file_search_attr = 0x2f;
	if( return_str == "") {
		call prompt;
		if(!return_int)
			goto exit;
	}
	put_box( x, y, x + w, y + l, 0, m_b_color, 'WILD CARD TAG SCAN', TRUE );
	set_vp( x + 1, y + 3, x + w - 3, y + l - 2 );
	gotoxy_vp( 1, 1 );
	draw_char( 196, x + 1, y + 2, m_b_color, w - 4 );
	write( 'SCANNING:', x + 1, y + 1, 0, m_t_color );
	call do_scan;



	set_vp( 1, 1, screen_width, screen_length );
	kill_box;
	goto exit;


do_scan:
	{
		++scan_level;
		fstr = fexpand(return_str);
		draw_char( 32, x + 12, y + 1, m_b_color, w - 14 );
		write( fstr, x + 12, y + 1, 0, m_s_color );
		text_color_vp = m_s_color;
		working;

			// Scan subdirectories
		if( sd ) {
			file_search_attr = 0x3f;
			jx = first_file( get_path(fstr) + "*.*" );
			while( jx == 0 ) {
				if( (check_key) && (key1 = 27)) {
					rm('userin^verify /T=Abort tag scanning?/H=metags^*');
					if( return_int ) {
						do_break = true;
					}
				}
				if( do_break )
					break;
				if( last_file_attr & 0x10 ) {
					if( (last_file_name != '.') && (last_file_name != '..') ) {
						set_global_str("SCAN_REC_" + str( scan_level ), file_search_rec );
						set_global_str("SCAN_NAME_" + str( scan_level ), fstr );
						return_str = get_path(fstr) + last_file_name + '\' + truncate_path(fstr);
						write_vp( return_str + "\n\r");
						call do_scan;
						file_search_rec = global_str("SCAN_REC_" + str( scan_level ) );
						fstr = global_str("SCAN_NAME_" + str( scan_level ) );
					}
				}
				jx = next_file;
			}
		}


		draw_char( 32, x + 12, y + 1, m_b_color, w - 14 );
		write( fstr, x + 12, y + 1, 0, m_s_color );
		file_search_attr = 0x2f;
    search_path = get_path( fstr ); // solve subdirectory proglems
		jx = first_file( fstr );
		file_search_attr = t_search_attr;
		while( (jx == 0) && !do_break ) {
			if( (check_key) && (key1 = 27)) {
				rm('userin^verify /T=Abort tag scanning?/H=metags^*');
				if( return_int ) {
					do_break = true;
				}
			}
			if( do_break )
				break;
			set_global_str("SCAN_REC_" + str( scan_level ), file_search_rec );
			write_vp( last_file_name );
			if( uo ) {
				int tt = last_file_time,
						tn;
        rm('tag_find_file /F=1/FN=' + search_path + last_file_name);  // because last_file_name
//        rm('tag_find_file /F=1/FN=' + last_file_name);              // does not include path
				if( return_int ) {
					tn = parse_int('þDT=', get_line );
          if( tt == tn ) {             // because user might have backed
//          if( tt <= tn ) {          // up to a previous version of the file
						write_vp("  ...file not changed");
						switch_window( tw );
						goto skip_build;
					}
				}
				switch_window( tw );
			}
			return_str = search_path + last_file_name;
			rm('ldfiles /NC=1/NHA=1');
			write_vp( '.' );
			if(error_level == 0) {
				rm('tag_build /WW=1/DT=' + str( last_file_time ));
			}
		skip_build:
			write_vp( ".\n\r" );
			error_level = 0;
			file_search_rec = global_str("SCAN_REC_" + str( scan_level ) );
			jx = next_file;
		}
		set_global_str("SCAN_REC_" + str( scan_level ), "");
		set_global_str("SCAN_NAME_" + str( scan_level ), "");
		--scan_level;
		ret;
	}



prompt:
	{
		int menu = menu_create,
						tw = window_id,
						menu_level = global_int('MENU_LEVEL');

		refresh = false;
		switch_window( window_count );
		create_window;
PROMPT_AGAIN:
		menu_set_item( menu, 1, 'wildcard Filespec:', return_str,
			 '/QK=10/C=1/W=40/ML=80/HISTORY=FILE_HISTORY',0,0, 0);

		menu_set_item( menu, 2, '', '', '/C=1/L=3/SDD=3/MSK=1',20,0, 0);
		menu_set_item( menu, 3, 'Directories', '', '/OL=1/DC=1/QK=1/C=34/L=6/W=14/HT=11/WIN=' + str(cur_window),15,0, 0);
		menu_set_item( menu, 4, 'Scan subdirectories   ', '', '/QK=2/C=34/L=3',13,sd, 0);
		menu_set_item( menu, 5, 'Newer files only      ', '', '/QK=3/C=34/L=4',13,uo, 0);
		menu_set_item( menu, 6, 'OK','', '/KC=<ENTER>/C=51/L=6/K1=13/K2=28/R=1',11,0, 0);
		menu_set_item( menu, 7, 'Cancel','', '/KC=<ESC>/C=51/L=8/K1=27/K2=1/R=0',11,0, 0);
		menu_set_item( menu, 8, 'Help','','/KC=<F1>/C=51/L=12/K1=0/K2=59/R=2/QK=1',11,0, 0);
		menu_set_item( menu, 9, 'Sort', 'DIRSHELL^DIRSORT /UPDATE=5','/C=51/L=14/M=1/QK=1',11,0, 0);
		menu_set_item( menu, 10, 'NameIt!','','/C=51/L=16/R=11/QK=5',11,0, 0);
		RETURN_INT = menu;
		RM('UserIn^Data_In /HN=1/T=WILCARD TAG SCAN/S=1/#=10/PRE=' + str( menu_level ) + '/POSG=@FPPOS@/H=metags^WILD' );
		return_str = menu_item_str( menu, 1, 2 );;
		if(  return_int == 11  ) {
			return_int = 0;
			RM('FILEMNGR^FM_FILE_LIST');
			if(  (Return_Int == 0)  ) {
				Goto PROMPT_AGAIN;
			}
		}
		if( return_int == 1 ) {
			sd = menu_item_int( menu, 4, 2 );
			uo = menu_item_int( menu, 5, 2 );
			set_global_int("TAG_SCAN_DIRS", sd );
		}
		menu_delete( menu );
		delete_window;
		switch_win_id( tw );
		ret;
	}



exit:
	message_row = t_message_row;
	set_global_int("MENU_LEVEL", global_int("MENU_LEVEL") - 1 );
	make_message('');
	delete_window;
	switch_win_id( ow );
}


macro tag_configure
		no_break
{
	int ow = window_id,
			menu = menu_create
			;
	str tstr = Global_Str( "METAGS_CONFIG" );
	refresh = false;

	return_str = parse_str( "/DF=", tstr );
	if( return_str == "" )
		return_str = "METAGS.TAG";
	menu_set_item( menu, 1, "Default tag file:", return_str, "/L=1/C=1/W=30/ML=80/QK=1", 0, 0, 0 );
	menu_set_item( menu, 2, "Tag \"#define\" in C/C++ files                          ", "", "/L=3/C=1/QK=1", 13,
															parse_int("/TD=", tstr),
															0
															);
	menu_set_item( menu, 3, "Locate tags in IMPLEMENTATION section of PASCAL files ", "", "/L=4/C=1/QK=42", 13,
															parse_int("/PI=", tstr),
															0
															);
	menu_set_item( menu, 4, "Wild-card scan updates only changed files             ", "", "/L=5/C=1/QK=1", 13,
															parse_int("/WU=", tstr),
															0
															);
	menu_set_item( menu, 5, "Browse files first                                    ", "", "/L=6/C=1/QK=1", 13,
															parse_int("/EB=", tstr),
															0
															);
	menu_set_item( menu, 6, "Store scanned file paths in tag file.                 ", "", "/L=7/C=1/QK=1", 13,
															parse_int("/SP=", tstr),
															0
															);

	menu_set_item( menu, 7, "Sort tags                                             ", "", "/L=8/C=1/QK=2", 13,
															parse_int("/SORT=", tstr),
															0
															);

	menu_set_item( menu, 8, " define Language specific tag files ",
															"DB /LT=LANGUAGE SPECIFIC TAG FILES/DT=DEFINE TAG FILE/F=METAGS/DPT=MULTITAGS.CFG",
                              "/L=10/C=12/QK=9/M=1/R=10", 11,
															0, 0 );
	menu_set_item( menu, 9, " add keys to Keymap ",
															"Tag_Keys",
                              "/L=12/C=4/QK=14/M=1/R=11", 11,
															0, 0 );

	menu_set_item( menu, 10, " add Multi-Tags to User menu ",
															"Tag_Add_To_User",
                              "/L=12/C=28/QK=20/M=1/R=11", 11,
															0, 0 );
	return_int = menu;
	rm("USERIN^DATA_IN /T=MULTI-TAGS CONFIGURATION/#=10/HN=1/H=metags^CONFIG" );
	if( return_int ) {
		set_global_str("METAGS_CONFIG", "" );

		rm("setconfig /DB=METAGS.DB/T=MULTITAGS.CFG/C=1");
		if( return_int ) {
			put_line("\fMULTITAGS.CFG /DF=" + menu_item_str( menu, 1, 2 ) +
																"/TD=" + str( menu_item_int( menu, 2, 2 ) ) +
																"/PI=" + str( menu_item_int( menu, 3, 2 ) ) +
																"/EB=" + str( menu_item_int( menu, 5, 2 ) ) +
																"/WU=" + str( menu_item_int( menu, 4, 2 ) ) +
																"/SP=" + str( menu_item_int( menu, 6, 2 ) ) +
																"/SORT=" + str( menu_item_int( menu, 7, 2 ) )
																);

		}
		rm("tag_init");
	}
	menu_delete( menu );
	switch_win_id( ow );
}

		/* */
macro tag_add_to_user
	trans
{
	int ow = window_id;
	refresh = false;
	rm("SETCONFIG /DB=MECONFIG/T=USER.MNU");
	if( !return_int ) {
		rm("MEERROR^messagebox /B=2/T=ERROR/M=Unable to find User menu in MECONFIG.DB");
		goto exit;
	}
	eol;
	mark_pos;
	reg_exp_stat = TRUE;
	ignore_case = TRUE;
	if( search_fwd("{metags}|\f",0) ) {
		if(found_str != "\f") {
      make_message("Multi-Tags Support already in your User Menu.");
			pop_mark;
			goto exit;
		}
	}
	goto_mark;
	cr;
	put_line(
"NAME=Multi-TagsCMD=metags^metagsTYPE=5QKID=!TAG_KEYCS=0"
			);
	if( global_int('~USER_MENU') ) {
		menu_delete( global_int('~USER_MENU') );
		set_global_int('~USER_MENU', 0 );
	}
	make_message("Multi-Tags added to user menu.");
exit:
	switch_win_id( ow );
}


macro tag_keys
	trans
{
	int ow = window_id;
	str kstr[10];

	refresh = FALSE;
	Return_Str = Parse_Str('FN=',Global_Str('@KEYMAP_NAME@'));
	if(  (Global_Str('@DB_EXTENSION') == '')  ) {
		Return_Str = Return_Str + '.DB';
	} else {
		Return_Str = Return_Str + '.' + Global_Str('@DB_EXTENSION');
	}
	RM('MakeUserPath /DF=1');
	if(  (Switch_File(Return_Str) == 0)  ) {
		Switch_Window(Window_Count);
		Create_Window;
		Error_Level = 0;
		Load_File(Return_str);
		if(  (Error_Level)  ) {
			delete_window;
			RM('MEERROR^MESSAGEBOX /B=2/T=ERROR ' + Str(Error_Level) + '/M=Can''t find ' + Return_Str +
					'.');
			Error_Level = 0;
			Goto exit;
		}
	}
	window_attr = $81;
	Tof;
	/* */
	reg_exp_stat = TRUE;
	if( search_fwd( "@@þ=-+ MULTI-TAGS -", 0 ) ) {
    make_message("Multi-Tags keys are already in your keymap.");
		goto exit;
	}
	eof;
	if( c_col > 1 )
			down;
	rm("userin^xmenu /B=1/L=Multi-Tags Keymap Insert/T=1/M=1-Insert assignments on <F11>(METAGS^CONFIG)2-Insert assignments on <F12>()3-Insert blank assignments()");
	if( return_int ) {
		if( return_int == 1 )
			kstr = "F11";
		else if ( return_int == 2 )
			kstr = "F12";
		else
			kstr = "";

	put_line(
"@þ=--------------------------------- MULTI-TAGS -----------------------------" );
	down;
	return_str = "";
	call make_kstr;
	put_line(
"MC=METAGSDESCR=Multi-TagsK1=" + return_str + "FKL=TAGSMID=!TAG_KEYMODE=1" );
	down;
	return_str = "Shft";
	call make_kstr;
	put_line(
"MC=TAG_LOCATEDESCR=Find tag under cursorK1=" + return_str + "FKL=GOTAGMF=METAGSMID=!TAG_KEY1MODE=1");
	down;
	return_str = "Ctrl";
	call make_kstr;
	put_line(
"MC=TAG_LOCATEPARAM=/NEXT=1DESCR=Find tag AGAINK1=" + return_str + "FKL=TAGAGNMF=METAGSMID=!TAG_KEY2MODE=1");
	down;
	return_str = "Alt";
	call make_kstr;
	put_line(
"MC=TAG_UPDATEDESCR=Update tags for current fileK1=" + return_str + "FKL=TAGUPDMF=METAGSMID=!TAG_KEY7MODE=1");
	down;
	put_line(
"MC=TAG_LISTDESCR=List tagsFKL=TAGLSTMF=METAGSMID=!TAG_KEY3MODE=1");
	down;
	put_line(
"MC=TAG_BROWSE_FILEDESCR=Browse current fileFKL=TAGBRWMF=METAGSMID=!TAG_KEY4MODE=1");
	down;

#IFDEF object_tree
	put_line(
"MC=TAG_OBJ_CURSORDESCR=Object hierarch of tagFKL=OBJTREMF=METAGSMID=!TAG_KEY9MODE=1");
	down;
#ENDIF

	put_line(
"MC=TAG_BUILDDESCR=Scan tags for current fileFKL=TAGSCNMF=METAGSMID=!TAG_KEY5MODE=1");
	down;
	put_line(
"MC=TAG_SCAN_FILESDESCR=Wildcard tag scanFKL=TAGWLDMF=METAGSMID=!TAG_KEY6MODE=1");
	}
	Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $02);
	Make_Message("Multi-Tags default key assignments created.");
	goto exit;

make_kstr:
	if( kstr == "" ) {
		return_str = "";
	}
	else {
		return_str = "<" + return_str + kstr + ">";
	}
	ret;
exit:
	switch_win_id( ow );
}

macro tag_init
	trans2
{
	int ow = window_id;
	if( global_str("METAGS_CONFIG") == "" ) {
		refresh = false;
		rm('meerror^messagebox /NW=1/M=Initializing Multi-Tags...');
		rm("setconfig /DB=METAGS.DB/T=MULTITAGS.CFG/C=1");
		set_global_str("METAGS_CONFIG", get_line );
		return_str = parse_str('/DF=', global_str("METAGS_CONFIG") );
		rm('XlateCmdLine');
		set_global_str("METAGS_FILE_NAME", return_str );
		down;
		if( search_fwd( "%@*@*@*@*START@*@*@*@*", 0 ) ) {
			down;
			while( !at_eof && (cur_char != "\f" )) {
				return_str = parse_str( '|127FILE=', get_line );
				rm('XlateCmdLine');
				set_global_str('TAG_F_' + parse_str('|127LS=', get_line ), return_str );
				down;
				goto_col(1);
			}
		}

		if( file_changed )
			save_file;
		delete_window;
		switch_win_id( ow );
		kill_box;
	}
}

macro tag_install
	dump
{
	rm("MEERROR^MessageBox /M=To complete the installation of Multi-Tags, we suggest that you select the buttons to automatically create User menu and keymap entries.  Hit <F1> from the configure dialog for detailed info.");
	rm("tag_configure");
}
