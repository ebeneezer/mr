macro_file fc;

/***************************************************************************
NAME: FC

DESCRIPTION:
 FC() performs a file compare on two files.  To use it, load up a file,
 and the run FC.  You will be prompted for a direction to split the
 window, and then you will be asked to select a file to compare the first
 one to.  The two files will be compared and then you are taken into
 "Synchronized compare mode".  The current line in both files will be
 synchronized, even across differences.  The differences in the files will
 be highlighted.  You can even edit the files, and FC will do its best to
 keep everything syncronized.  This macro makes use of the event_macro
 window variable to intercept all keystrokes going to the compared
 windows.


NOTES:
 Most file compares are oriented towards the concept of an "original" file
 and a "changed" file.  They then generate there output based on insertions
 and deletions in the "original file".  This macro does not make any
 assumptions about "original" or "changed" files.  Instead, it simply locates
 the differences, and records them for both files.


INTERFACE
	 run FC for the actual file compare
	 run FC_CONFIG for the file compare configuration.
	 run FC_REPORT to generate a report on the compare

   All other functions are internal to fc, and should NOT be executed by the
	 user.


FUNCTIONS:
   fc_resynch       resynchronizes the two windows after a difference
   fc_compare       performs the actual compare
   fc_match_pos     keeps the two windows synchronized during sync mode
   fc_next_dif      takes the cursor to the next/previous difference
	 fc_report				generates readable reports on the differences
	 fc_reset					gets rid of the compare color coding
	 fc_config				configuration dialog
	 fc_config_x			used by fc_config
	 fc_event_proc    process all keystrokes to the compare windows
	 fc_update				updates the compare data after lines are inserted or
										deleted
   fc               the main macro function

AUTHOR:   Todd Johnson

HISTORY:  03-18-93 01:52pm  Original release
					04-08-93 01:39am  Re-wrote fc_resynch to provide for more accurate,
														faster resynching.  Added fc_config to allow
														for easy configuration
					04-19-93 03:58pm  Revamped FC to take advantage of the event_macro
														window variable.  FC is now "modeless".  The
														file compare  is run, and then control is
														returned to the user.  You can now edit the
														files just like any other, and fc will try to
														keep the files synced.


            (C)Copyright 1993 by American Cybnernetics, Inc.
***************************************************************************/

prototype mesys
{
	void cursor_to_line( int l );
}

global
{
	str fc_config_str;
}



/*********************************************************************
 Resynchs the two windows, thus locating the end of the difference
 section.  Returns 1 if successful, 0 if the end of both files was
 reached without resynching.
   w1 is the first window
   w2 is the second window
	 l1,l2 the line numbers where the difference occurs
	 eofl1, eofl2  the ending line #s of both files

   l1 and l2 return the ending line numbers (where the resynch occured)
 *********************************************************************/
int fc_resynch( int w1, w2, &l1, &l2, eofl1, eofl2 )
{
	str s[2048];
  int lx, eof1 = 0, eof2 = 0, eofx,
			rx1 = 0,rx2 = 0, rl1, rl2,
			ll1,ll2, blank_count,blank_count2,
			fc_changed_attr = parse_int("/CC=", fc_config_str);

		// ll1 and ll2 point to the current line while resynching.  We don't want
		// to disturb the contents of l1 or l2 until the end.
	ll1 = l1;
	ll2 = l2;
	reg_exp_stat = true;
	ignore_case = false;
	do
	{

			// Get next non-block line in window 1
		blank_count = 0;
		while( (ll1 <= eofl1) && (shorten_str(s = get_line_from_win( ll1, w1)) == ""))
		{
			++ll1;
			++blank_count;
		}
			// Go to window 2 and search for the line
		switch_window( w2 );
		goto_line( l2 );
		goto_col(1);
		if( search_fwd( "%" + make_literal(s) + "$", ll2 - c_line + 1 ))
		{
				// If we matched, mark the spot and break the loop
			rx2 = c_line;
			rl2 = ll1;
			blank_count2 = 0;
			while((blank_count > 0) && (c_line > l2))
			{
				up;
				if(shorten_str(get_line) != "")
				{
					down;
					break;
				}
				--blank_count;
				++blank_count2;
			}
			break;
		}

			// Get next non-blank line in window 2
		blank_count = 0;
		while( (ll2 <= eofl2) && (shorten_str(s = get_line_from_win( ll2, w2)) == ""))
		{
			++ll2;
			++blank_count;
		}

			// Go to window 1 and search for the line
		switch_window( w1 );
		goto_line( l1 );
		goto_col(1);
		if( search_fwd( "%" + make_literal(s) + "$", ll1 - c_line + 1 ))
		{
				// If we matched, mark the spot and break the loop
			rx1 = c_line;
			rl1 = ll2;
			blank_count2 = 0;
			while((blank_count > 0) && (c_line > l1))
			{
				up;
				if(shorten_str(get_line) != "")
				{
					down;
					break;
				}
				--blank_count;
				++blank_count2;
			}
			break;
		}

			// Point to next line in both windows
		if( ll1 < eofl1)
			++ll1;
		if( ll2 < eofl2)
			++ll2;
	} while(!(( ll1 >= eofl1)  && ( ll2 >= eofl2)));

	// if we found a point that we can resynch, then take the shortest path
	if( rx1 || rx2 )
	{
		if( rx1)
		{
			switch_window( w2 );
			goto_line( l2 );
			while( c_line < (rl1 - blank_count2) )
			{
				line_attr = fc_changed_attr;
				down;
			}
			l2 = rl1;
			switch_window( w1 );
      lx = rx1;
			goto_line(l1);
			l1 = lx;
		}
		else
		{
			switch_window(w1);
			goto_line( l1 );
			while( c_line < (rl2 - blank_count2) )
			{
				line_attr = fc_changed_attr;
				down;
			}
			l1 = rl2;
			switch_window( w2 );
      lx = rx2;
			goto_line(l2);
			l2 = lx;
		}
		while(c_line < (lx - blank_count2))
		{
			line_attr = fc_changed_attr;
			down;
		}
		return(1);
	}

	switch_window(w1);
	goto_line(l1);
	switch_window(w2);
	goto_line(l2);
	return(0);

}


/*******************************************************************
 This routine performs the actual comparison of two windows.
	w1		The window # of the first file to compare
	w2		The widnow # of the second file to compare
	tw		The window # where the change info is to be stored
	c1		The starting line # of window 1 (returns the ending line #)
	c2    The starting line # of window 2 (returns the ending line #)
	line_count1  The # of lines to compare in window 1
	line_count2  The # of lines to compare in window 2
	twl    Returns the total # of differences

Returns FALSE if no resynch occurred on the last difference
********************************************************************/
int fc_compare( int w1, w2, tw, &c1, &c2, line_count1, line_count2,
									  &twl, true_eof1, true_eof2 )
{
	int fx, eof1 = 0, eof2 = 0,l1,l2, eofl1, eofl2, result = TRUE;
	str s1,s2;
	int tt = system_timer;
	int highlighting = keyword_highlighting;
	int	fc_insert_attr = parse_int("/IC=", fc_config_str);


	keyword_highlighting = FALSE;

	switch_window( tw );
	eof;
	if((c_line==1) && (c_col == 1))
	{
		twl = 0;
	}
	else
		twl = c_line;

		// Save the formatlines
	switch_window( w1 );
  set_global_str("!FC_FL1", format_line );
	eofl1 = c1 + line_count1;
	switch_window( w2 );
  set_global_str("!FC_FL2", format_line );
  format_line = "";
	switch_window(w1);
  format_line = "";
	eofl2 = c2 + line_count2;
	switch_window(w2);

// The main compare loop
	do
	{
		call find_difference;

			// if we found a difference, then perform resynching
		if(fx)
		{
				// Store the starting difference point
			put_line_to_win("/C1=" + str(l1) + "/C2=" + str(l2), ++twl, tw, false );

				// Start the resynch process
			if (!fc_resynch( w1, w2, l1, l2 ,true_eof1, true_eof2))
			{
				c1 = true_eof1;
				c2 = true_eof2;
				call put_s;
				result = false;
				goto dif_done;
			}

			c1 = l1;
			c2 = l2;
			call put_s;
		}
		else
			goto dif_done;
		switch_window(w1);
	}
	while( !(eof1 && eof2) );

dif_done:

	// Restore the formatlines
  switch_window(w1);
  format_line = global_str("!FC_FL1");
  set_global_str( "!FC_FL1", "" );
  switch_window(w2);
  format_line = global_str("!FC_FL2");
  set_global_str( "!FC_FL2", "" );
	keyword_highlighting = highlighting;
	return(result);

// The /Sx= (same again) line to the change log file
put_s:
	put_line_to_win("/S1=" + str(l1) + "/S2=" + str(l2), ++twl, tw, false);
	ret;

	// Find the start of a difference section
	// fx will be 1 if a difference was found, otherwise 0
find_difference:
	fx = 0;
	do
	{
		call get_line1;
		call get_line2;
//    put_line_num(c1);
		if( s1 != s2)
		{
			l1 = c1;
			l2 = c2;
			switch_window( w2 );
			goto_line( l2 );
			line_attr = fc_insert_attr;
			switch_window(w1);
			goto_line( l1 );
			line_attr = fc_insert_attr;
			fx = 1;
			ret;
		}
		++c1;
		++c2;
	}
	while( !(eof1 && eof2) );
	l1 = c1;
	l2 = c2;
	ret;

	// Get line from window one.
get_line1:
	{
		if( c1 > eofl1 )
		{
			eof1 = TRUE;
		}
		s1 = get_line_from_win( c1, w1 );
		ret;
	}

	// Get line from window two.
get_line2:
	{
		if( c2 > eofl2 )
		{
			eof2 = TRUE;
		}
		s2 = get_line_from_win( c2, w2 );
		ret;
	}

}

/*********************************************************************
  This routine is used to keep the cursors in synch after the actual
  file compare has been done.
 *********************************************************************/
void fc_match_pos( int w1, w2, tw, &l1, &l2 )
{
	int jx, tundo = undo_stat,m;

	str ss1[4], ss2[4], cc1[4], cc2[4];

	set_virtual_display;

	undo_stat = false;

	if(window_id == w2 )
	{
		m = 1;
		jx = w2;
		w2 = w1;
		w1 = jx;
		ss1 = "/S2=";
		ss2 = "/S1=";
		cc1 = "/C2=";
		cc2 = "/C1=";
	}
	else
	{
		m = 0;
		ss2 = "/S2=";
		ss1 = "/S1=";
		cc2 = "/C2=";
		cc1 = "/C1=";
		switch_win_id(w1);
	}

	int tl = c_line, nl = 0, s1,s2, c1,c2, old_c_row = c_row, new_c_row = -1;
	refresh = false;

	switch_win_id( tw );
	tof;
	while( !at_eof )
	{
		if(parse_int(cc1, get_line) > tl )
		{
			if( c_line == 1 )
			{
				nl = tl;
			}
			else
			{
				up;
				s1 = parse_int(ss1, get_line );
				s2 = parse_int(ss2, get_line );
				nl = s2 + (tl - s1);
			}
			new_c_row = old_c_row;
			goto done;
		}
		down;
		if( parse_int(ss1, get_line) > tl )
		{
			up;
			nl = parse_int(cc2, get_line);
			new_c_row = old_c_row;
			goto done;
		}
		down;
	}
	up;
	if(!at_eof) {
		s1 = parse_int(ss1, get_line );
		s2 = parse_int(ss2, get_line );
		nl = s2 + (tl - s1);
	}
	else
		nl = tl;
	new_c_row = old_c_row;
done:
	if ( nl < 1 )
	{
		nl=1;
	}
	if( m )
	{
		l2 = c_line;
	}
	else
		l1 = c_line;
	switch_win_id( w2 );
	if( new_c_row != -1 )
	{
		if( new_c_row > (win_y2 - win_y1 - 1))
		{
			new_c_row = win_y2 - win_y1 - 1;
		}
		while(c_row < new_c_row )
			down;
		while(c_row > new_c_row )
			up;
		goto_line(nl);
	}
	else
		cursor_to_line( nl );
  // refresh = true;
  // update_window;
	if( m )
	{
		l1 = c_line;
	}
	else
		l2 = c_line;
  refresh = true;
	if(!xpos("-Cmpr", window_name, 1 ))
	{
		window_name += "-Cmpr";
    update_window;
	}
  refresh = false;
	switch_win_id( w1 );
	if(!xpos("-Cmpr", window_name, 1 ))
	{
		window_name += "-Cmpr";
    refresh = true;
    update_window;
	}
	refresh = false;
	undo_stat = tundo;
	update_virtual_display;
	reset_virtual_display;
}


/****************************************************************
 Takes the cursor to the next or previous difference
    d == 0, goto next difference
    d == 1, goto previous difference
    w1      the first compare window id
    w2      the second compare window id
    tw      the temporary window id
 ****************************************************************/
int fc_next_dif(int d, w1, w2, tw )
{
  int jx, tundo = undo_stat,m,  cl = c_line, jl,
      result = true;

  str cc1[4], cc2[4];
	undo_stat = false;

	set_virtual_display;

	if(window_id == w2 )
	{
    jx = w2;
		w2 = w1;
		w1 = jx;
		cc1 = "/C2=";
		cc2 = "/C1=";
	}
	else
	{
		cc2 = "/C2=";
		cc1 = "/C1=";
		switch_win_id(w1);
	}
  int s1,s2;
	refresh = false;
  reg_exp_stat= true;
	switch_win_id( tw );
	tof;
	while( !at_eof && (jl = parse_int( cc1, get_line ) <= cl ))
	{
		down;
		down;
	}
  if(d)
  {
		do
		{
			up; eol;
			jl = search_bwd("/C",0);
		}
    while( (jl !=0) && (c_line > 1) && (parse_int( cc1, get_line ) >= cl ));
		if( !jl )
		{
			goto done;
		}
  };
	if( !at_eof )
	{
	  s1 = parse_int(cc1, get_line);
	  s2 = parse_int(cc2, get_line);
		switch_win_id( w2 );
		jx = (win_y2 - win_y1) / 2;
		if( jx < 1 )
			jx = 1;
		while(c_row > jx)
			up;
		while(c_row < jx)
			down;
	  goto_line( s2 );
	  refresh = true;
	  switch_win_id( w1 );
		jx = (win_y2 - win_y1) / 2;
		if( jx < 1 )
			jx = 1;
		while(c_row > jx)
			up;
		while(c_row < jx)
			down;
		goto_line( s1 );
    update_window;
	}
	else
	{
    result = false;
		switch_win_id(w1);
	}

done:
	undo_stat = tundo;
	update_virtual_display;
	reset_virtual_display;
  return(result);
}


/******************** Multi-Edit VOID Macro Function ************************
 NAME:         fc_reset

 DESCRIPTION:  resets all of the line color attributes in a file

 PARAMETERS:   w  the window id to reset
*****************************04-08-93 01:46am*******************************/
void fc_reset( int w )
{
	int tr = refresh,
			tw = cur_window;
	refresh = false;
  if( switch_win_id( w ))
	{
		mark_pos;
		tof;
		while(!at_eof)
		{
			line_attr = 0;
			down;
		}
		goto_mark;
		rm("Set_Syntax_Parms");
	}
	switch_window( tw );
	refresh = tr;
}

/******************************************************************
 This function generats a report on the file compare.
 It has two modes:
     report_mode 0    Make a short report, showing only line #s
     report_mode 1    Make a detailed report, showing the actual text
 *****************************************************************/
void fc_report( )
{
  int report_win, ln = 0, cc1, cc2, ss1, ss2, jx;

	int w1 = parse_int("/W1=", event_macro),
			w2 = parse_int("/W2=", event_macro),
			tw = parse_int("/TW=", event_macro),
      wn1,wn2,twn,
			report_mode = 0,
			twin = window_id;

  refresh = false;

	if( !switch_win_id( tw ) )
	{
		rm("MEError^MessageBox /T=ERROR/B=1/M=No valid file compare information available.");
		return();
	}
  twn = cur_window;

	RM("Xmenu /L=Select report type/B=1/T=1/M=Summary report(FC)Detailed report(FC)");
	if( return_int < 1 )
	{
		switch_win_id(twin);
		return();
	}

	report_mode = return_int - 1;

  rm("CreateWindow");
  rm("SetWindowNames");
  file_name = "FCREPORT.TXT";
  report_win = cur_window;
  switch_win_id( w1 );
  wn1 = cur_window;
  put_line_to_win( "File compare: " + date + " " + time, ++ln, report_win, false );
  put_line_to_win( "File 1: " + file_name, ++ln, report_win, false );
  switch_win_id( w2 );
  wn2 = cur_window;
  put_line_to_win( "File 2: " + file_name, ++ln, report_win, false );
  ++ln;

  switch_win_id( tw );
  tof;
  while( !at_eof )
  {
    cc1 = parse_int( "/C1=", get_line );
    cc2 = parse_int( "/C2=", get_line );
    down;
    ss1 = parse_int( "/S1=", get_line );
    ss2 = parse_int( "/S2=", get_line );
    down;
    put_line_to_win("Difference: File 1, lines " + str(cc1) + " to " + str(ss1), ++ln, report_win, false );
    put_line_to_win("            File 2, lines " + str(cc2) + " to " + str(ss2), ++ln, report_win, false );
    if( report_mode )
    {
      ++ln;
      put_line_to_win("  File 1 detail:", ++ln, report_win, false );
      for( jx = cc1; jx <= ss1; jx++ )
      {
        put_line_to_win("   " + str( jx ) + ": " + get_line_from_win( jx, wn1 ), ++ln, report_win, false );
      }
      ++ln;
      put_line_to_win("  File 2 detail:", ++ln, report_win, false );
      for( jx = cc2; jx <= ss2; jx++ )
      {
        put_line_to_win("   " + str( jx ) + ": " + get_line_from_win( jx, wn2 ), ++ln, report_win, false );
      }
      ++ln; ++ln;
    }
  }
  switch_window( report_win );
  return();
}

/******************** Multi-Edit VOID Macro Function ************************

 NAME:         FC_Set_Keynames()

 DESCRIPTION:  Sets the keyname message str.

*****************************07-29-93 10:36pm*******************************/
void FC_Set_Keynames()
{
	str key_str[80],tstr;
  // Find current key assignments
  int tmp_int = parse_int("/NKEY=", fc_config_str);
  rm('setup^makekey /K1='+str(tmp_int%256)+'/K2='+str(tmp_int>>8));
  key_str = 'NextDif'  + return_str;
  tmp_int = parse_int("/PKEY=", fc_config_str);
  rm('setup^makekey /K1='+str(tmp_int%256)+'/K2='+str(tmp_int>>8));
  key_str += ' PrevDif' + return_str;
  tmp_int = parse_int("/RKEY=", fc_config_str);
  rm('setup^makekey /K1='+str(tmp_int%256)+'/K2='+str(tmp_int>>8));
  key_str += ' Report'+return_str;
	tstr = fc_config_str;
	var_remove_str('/KEYSTR=',tstr);
	fc_config_str = tstr + '/KEYSTR='+key_str;

}


/****************** Multi-Edit INTEGER Macro Function ***********************

 NAME:         fc_config()

 DESCRIPTION:  configuration dialog for fc

 PARAMETERS:

 RETURNS:      TRUE if accepted, FALSE if ESC was hit.

*****************************04-09-93 01:18am*******************************/
int fc_config( int set_defaults = parse_int("/D=", mparm_str) )
	trans
{

	str tstr = "";

	int menu = menu_create,
			tr = refresh,
			change_color,
			insert_color;

	refresh = false;
	if( fc_config_str == "" )
	{
		fc_config_str = "/SH=0/CC=79/IC=48/RW=37120/NKEY=37120/PKEY=36096/RKEY=5118";
	}

	if ( set_defaults )
	{
		return(1);
	}
	tstr = fc_config_str;

	var_parse_int("/CC=", tstr, change_color );
	var_parse_int("/IC=", tstr, insert_color );

	menu_set_item( menu, 1, "Syntax highlighting during interactive compare", "", "/L=1/C=1/QK=1", 13,
															parse_int("/SH=", tstr), 0 );
	menu_set_item( menu, 2, "Next difference key:    ", "", "/QK=1/L=3/W=20/C=1", 9,parse_int("/NKEY=", tstr), 0 );
	menu_set_item( menu, 3, "Previous difference key:", "", "/QK=1/W=20/L=4/C=1", 9,parse_int("/PKEY=", tstr), 0 );
	menu_set_item( menu, 4, "Report key:             ", "", "/QK=1/W=20/L=5/C=1", 9,parse_int("/RKEY=", tstr), 0 );

	int color_item = 8;
	menu_set_item( menu, 5, "Colors:", "", "/L=7/C=1/QK=1", 10,0, 0 );
	menu_set_item( menu, 6, " Unchanged line  ", "", "/L=8/C=2/ATTR=" + str(w_t_color), 10,0, 0 );
	menu_set_item( menu, 7, "                 ", "", "/L=9/C=2/ATTR=" + str(w_t_color), 10,0, 0 );
	menu_set_item( menu, 8, " Lines different ", "", "/L=10/C=2/ATTR=" + str(change_color), 10,0, 0 );
	menu_set_item( menu, 9, "                 ", "", "/L=11/C=2/ATTR=" + str(w_t_color), 10,0, 0 );
	menu_set_item( menu,10, " Lines inserted  ", "", "/L=12/C=2/ATTR=" + str(insert_color), 10,0, 0 );
	menu_set_item( menu,11, "                 ", "", "/L=13/C=2/ATTR=" + str(w_t_color), 10,0, 0 );

	menu_set_item( menu, 12, "  1 ", "fc_config_x /#=" + str(color_item) + "/M=" + str(menu), "/QK=4/L=10/C=20/M=1/R=4", 11, 0, 0 );
	menu_set_item( menu, 13, "  2 ", "fc_config_x /#=" + str(color_item + 2) + "/M=" + str(menu), "/QK=4/L=12/C=20/M=1/R=5", 11, 0, 0 );

	return_int = menu;
  rm("USERIN^DATA_IN /T=File Compare Configuration/#=13/HN=1/H=COMPARE" );

	if(return_int)
	{
		fc_config_str = "/SH=" + str(menu_item_int( menu, 1, 2 )) +
										"/CC=" + parse_str("/ATTR=", menu_item_str( menu, color_item,3)) +
										"/IC=" + parse_str("/ATTR=", menu_item_str( menu, color_item + 2,3)) +
										"/RW=" + str(menu_item_int( menu, 2, 2 )) +
										"/NKEY=" + str(menu_item_int( menu, 2, 2 )) +
										"/PKEY=" + str(menu_item_int( menu, 3, 2 )) +
										"/RKEY=" + str(menu_item_int( menu, 4, 2 ))
										;
		fc_set_keynames;
	}

	menu_delete(menu);
	refresh = tr;
	return(return_int);
}

void fc_config_x()
	trans2
{
	int menu = parse_int( "/M=", mparm_str);
	int mn = parse_int("/#=", mparm_str);
	int attr;

	str tstr;

	tstr = menu_item_str( menu,  mn, 3 );
	attr = parse_int("/ATTR=", tstr );
	var_remove_str( "/ATTR=", tstr );

	return_int = attr;
	rm( "SETUP^COLORCHART");

	if(return_int >= 0)
		attr = return_int;

	tstr += "/ATTR=" + str(attr );
	menu_set_str( menu, mn, 3, tstr );
	return_int = 0;
}


/******************** Multi-Edit VOID Macro Function ************************

 NAME:         fc_update

 DESCRIPTION:  Handles updating the fc data after lines have been inserted
							 or deleted.  Should only be called by fc_event_proc
							 The update is done by taking the offset of ol and nl.

 PARAMETERS:   ol				old line number
							 nl				new line number
							 wid1, wid2  compare window ids
							 twid     compare data window id


*****************************04-19-93 04:27pm*******************************/
void fc_update( int ol, nl, wid1, wid2, twid )
{
	int tr = refresh, twin = cur_window,
	    tl, offset;
	str tc[4],ts[4], tstr[80];

	refresh = false;

	offset = nl - ol;		// get offset

	if(window_id == wid1)  // Determine the data that is being updated
	{
		tc = "/C1=";
		ts = "/S1=";
	}
	else
	{
		tc = "/C2=";
		ts = "/S2=";
	}

	if(switch_win_id(twid))
	{
		tof;                      // process whole file
		while(!at_eof)
		{
			tstr = get_line;
			tl = parse_int(tc,tstr);
			if(tl >= ol)
			{
				tl = tl + offset;
				var_remove_str( tc, tstr );
				tstr += tc + str(tl);
				put_line(tstr);
			}
			else
			{
				tl = parse_int(ts,tstr);
				if(tl >= ol)
				{
					tl = tl + offset;
					var_remove_str( ts, tstr );
					tstr += ts + str(tl);
					put_line(tstr);
				}
			}
			down;
		}

		switch_window(twin);
	}


	refresh = tr;
}

/****************** Multi-Edit INTEGER Macro Function ***********************

 NAME:         fc_event_proc()

 DESCRIPTION:  Even processor for file compare windows

*****************************04-18-93 02:04pm*******************************/
int fc_event_proc()
{
	int wid1 = parse_int("/W1=", mparm_str),
			wid2 = parse_int("/W2=", mparm_str),
      bid = buffer_id,
			twid = parse_int("/TW=", mparm_str),
      cl1 = c_line + 1, ww = window_id,
			cl,cp,crr,co,
			l1,l2;

	// Check for next/previous difference keys
	l1 = (key2 << 8) + key1;
	if(l1 == parse_int("/NKEY=", fc_config_str))
	{
	  fc_next_dif( 0, wid1, wid2, twid );
	}
	else if(l1 == parse_int("/PKEY=", fc_config_str))
	{
	  fc_next_dif( 1, wid1, wid2, twid );
	}
	else if(l1 == parse_int("/RKEY=", fc_config_str))
	{
		fc_report;
	}
	else
	{

		// Set a mark in the third marker set
		set_mark_record( 1, 3, cl1, c_col,c_row + 1, left_offset );

		// Execute the user's operation
		pass_event_through;

		// If we are still in the same window then process
		if(ww==window_id)
		{
			// Get the mark we set
			get_mark_record( 1, 3, cl, cp, crr, co );

			// If the lines do not match, then something was inserted or deleted
			if(cl != cl1)
			{
				// beep;
				// Update the tmp window
				fc_update( cl1, cl, wid1, wid2, twid );
			}

			// Now synch the positions
			fc_match_pos(wid1,wid2,twid, l1, l2 );
			call display_keys;
		}
		else   // Check for the existance of the windows.  If either of them is
					 // gone, then remove the tmp window and cancel the compare
		{
			int tr = refresh;

			ww = window_id;
			refresh = false;

			if( !switch_win_id( wid1 ) )
			{
				if( switch_win_id( wid2 ) )
				{
					event_macro = "";
				}
				call cleanup;
			}
			else
			if( !switch_win_id( wid2 ) )
			{
				if( switch_win_id( wid1 ) )
				{
					event_macro = "";
				}
				call cleanup;
			}
			switch_win_id( ww );
			if(ww != wid2)
				make_message("");
			refresh = tr;
		}
	}
	goto done;


display_keys:
  make_message(parse_str('/KEYSTR=', fc_config_str));
	ret;


  int clean_id;

id_win:
  if( !switch_win_id( clean_id ) && bid )
  {
    switch_window(1);
    while((cur_window < window_count) && (buffer_id != bid))
    {
      switch_window( cur_window + 1 );
    }
    if(buffer_id == bid)
    {
      clean_id = window_id;
      bid = 0;
    }
  }
  ret;

cleanup:
	if( switch_win_id( twid ) )
	{
		delete_window;
	}

  clean_id = wid1;
  call id_win;
  fc_reset( clean_id );
  clean_id = wid2;
  call id_win;
  fc_reset( clean_id );
	make_message("");
	ret;


done:
	return(0);
}


/******************** Multi-Edit VOID Macro Function ************************

 NAME:         fc()

 DESCRIPTION:    This is the main file compare macro fc_mode lets you
								 override whether or not this is a new compare Normally,
								 fc automatically detects whether or not to start a new
								 compare, based on the window you are in.

 PARAMETERS:

 RETURNS:        Nothing

*****************************04-09-93 01:29am*******************************/
void fc( int fc_mode = parse_int("/M=", mparm_str) )
{
	int w1 = cur_window, w2, tw, wn, highlighting;
	int wid1 = window_id, wid2, c1 = 1, c2 = 1, lc1, lc2, lx1, lx2, cc1, cc2;
  int tt, dc = 0, jx, fc_done = false;
	int fc_tmp_win;

	refresh = false;

	if( fc_config_str == "" )
	{
		fc_config( true );
	}

  fc_tmp_win = parse_int("/TW=", event_macro );


		// look for our file compare temp window
		// if it doesn't exist, then create it
	if(  !switch_win_id(fc_tmp_win) ||  (fc_tmp_win == 0) )
	{
		create_window;
		window_attr = $81;
		fc_tmp_win = window_id;
	}
		// now erase it
	if( switch_win_id(fc_tmp_win ) )
	{
		erase_window;
	}
  // file_name = "FC.TMP";
  // window_attr  = 0;
	tw = cur_window;

  // Return to original window
  switch_win_id( wid1 );

  // Get the ids from the last compare
  wid1 = parse_int("/W1=", event_macro );
  wid2 = parse_int("/W2=", event_macro );

  // Check to see if we are in one of the windows from the last compare
  // If we are, then check for the existance of the second window
  // If both are true, then start from where we left off with the last compare
  // instead of restarting.
	if(window_id == wid1)
	{
		if(switch_win_id( wid2 ))
		{
			fc_mode=true;
		}
	}
	else if(window_id == wid2)
	{
		if(switch_win_id( wid1 ))
		{
			fc_mode=true;
		}
	}

	if( fc_mode )
	{
		fc_mode=false;
		if (switch_win_id( wid1 ))
		{
			w1 = cur_window;
			if( switch_win_id( wid2 ) )
			{
				w2 = cur_window;
				fc_mode=true;
			}
		}
	}

  // if fc_mode == 0, start a new compare
	if( !fc_mode )
	{
		switch_window( w1 );
		wid1 = window_id;
    rm("SPLITWIN /WL=1");
		redraw;
		refresh = false;
		if(return_int)
		{
			wid2 = window_id;
			w2 = cur_window;
		}
		else
			return();
	}
	else
	{
		working;
    fc_reset( wid1 );
    fc_reset( wid2 );
	}

	highlighting = parse_int("/SH=", fc_config_str);

	if ( !highlighting )
	{
		keywords_str = "";
	}

	mark_pos;
	eof;
	lc2 = c_line;
	tof;
	switch_window( w1 );
	if ( !highlighting )
	{
		keywords_str = "";
	}
	mark_pos;
	eof;
	lc1 = c_line;
	tof;

	make_message( "Comparing files...");
	call compare;

	push_labels;

	refresh = true;
	switch_window( w1 );
  event_macro  = "FC_EVENT_PROC /W1=" + str(wid1) + "/W2=" + str(wid2) + "/TW=" + str( fc_tmp_win );
	wn = cur_window;
	if( fc_mode )
	{
		goto_mark;
	}
	else
	{
		tof;
	}
	redraw;
	cc1 = c_line;
	switch_window( w2 );
  event_macro  = "FC_EVENT_PROC /W1=" + str(wid1) + "/W2=" + str(wid2) + "/TW=" + str( fc_tmp_win );
	if( fc_mode )
	{
		goto_mark;
	}
	else
		tof;
	cc2 = c_line;
	redraw;

		// If this is a new compare, then go to the first difference
	if( !fc_mode )
	  fc_next_dif( 0, wid1, wid2, fc_tmp_win );

  refresh = true;
  redraw;
	fc_set_keynames();
  make_message(parse_str('/KEYSTR=', fc_config_str));
	return();

  // Perform the next part of the compare
compare:
	refresh = false;
	make_message( "Comparing files...");
	working;
		// Mark positions in windows
	wn = window_id;
	switch_win_id( wid1 );
	w1 = cur_window;
	set_mark(1);
	switch_win_id( wid2 );
	w2 = cur_window;
	set_mark(1);

	lx1 = lc1;
	lx2 = lc2;
	// Do the compare
	fc_compare( w1, w2, tw, c1, c2, lx1, lx2, dc, lc1, lc2 );
	switch_window( w1 );
	get_mark(1);
	switch_window( w2 );
	get_mark(1);
	switch_win_id( wn );
 	// fc_match_pos(w1,w2,tw, cc1, cc2);
	make_message( "Compare complete." );
	ret;

}
