macro_file hex;
/******************** Multi-Edit VOID Macro Function ************************

 NAME:         HexWindow()

 DESCRIPTION:  Sets up a window for simultaneous HEX/ASCII editing, or
							 turns a window that is ALREADY in hex mode back to normal

 PARAMETERS:

 RETURNS:

*****************************04-10-93 11:25pm*******************************/
void HexWindow()
  trans2
{
	int w1 = window_id, wn1 = cur_window, w2;
	int tr = refresh, ln = (win_x2  - win_x1 - 1) / 4;
	refresh = false;

	// If already in hex mode, then turn hex mode off and exit
  if( (link_stat > 0) && (xpos("HexEventProc", event_macro, 1)))
	{
		w2 = parse_int("/W=", event_macro);
		window_attr = 0;
		event_macro = "";
		if(switch_win_id(w2))
		{
			rm("DelWin");
			switch_win_id(w1);
		}
		redraw;
		refresh = TR;
		return();
	}

	rm("Window^MakeWin /NL=1");
	link_window( wn1 );
	w2 = window_id;
	switch_win_id( w1 );

	rm("WINDOW^SplitWin /DIR=RIGHT/LN#=" + str( ln + 1 ) + "/ID=" + str(w2));
	switch_win_id( w2 );
  event_macro = "Hex^HexEventProc /W=" + str( w1 );
	window_attr &= 0xDF;
	switch_win_id( w1 );
  event_macro = "Hex^HexEventProc /W=" + str( w2 );
	window_attr |= 0x20;
	redraw;
	refresh = tr;
}


#DEFINE hex_chars "0123456789ABCDEF"

/****************** Multi-Edit INTEGER Macro Function ***********************

 NAME:         HexEventProc()

 DESCRIPTION:  This is the event processor for simultaneous HEX/ASCII editing.
							 This is only used by HexWindow.  This is not to be called by
							 the user.

 PARAMETERS:

 RETURNS:      True if the event processor should go ahead and handle the event.

*****************************04-10-93 11:26pm*******************************/
int HexEventProc()
    trans2
{
	int w1 = window_id, w2 = parse_int("/W=", mparm_str);
	int tr = refresh;
	int tc1,tc2,tw;
	if(!parse_int("/M=", mparm_str))
	{
		if ( window_attr & 0x20 )
		{
			tc1 = xpos( caps(char(key1)), hex_chars, 1 );
			if( tc1 != 0)
			{
				push_undo;
				--tc1;
				int ti = insert_mode;
				insert_mode = false;
				text( char( tc1 ) );
				left;
				gotoxy(wherex + 1, wherey );
				read_key;
				tc2 = xpos( caps(char(key1)), hex_chars, 1 );
				insert_mode = ti;
				if( tc2 != 0)
				{
					--tc2;
					insert_mode = false;
					text( char( (tc1 << 4) | tc2  ) );
					insert_mode = ti;
					left;
				}
				else
				{
					call check_pass_through;
				}
				pop_undo;
			}
			else
			{
				call check_pass_through;
			}
		}
		else
			pass_event_through;
	}

	if( window_id == w1)
	{
update:
		int t_line = c_line, t_col = c_col, t_c_row = c_row;

		refresh = true;
		set_virtual_display;
		refresh = false;
		if( switch_win_id(w2))
		{
			int tundo = undo_stat;
			undo_stat = false;
			while( c_row > t_c_row )
				up;

			while( c_row < t_c_row )
				down;

			goto_line( t_line );
			goto_col( t_col );
			refresh = true;
			update_window;
			int wx1 =  wherex;
			int wy1 = wherey;
			int al = 1 + ((window_attr & 0x20) != 0);
			switch_win_id( w1 );
			redraw;
			draw_attr( wx1,  wy1, (l_color >> 4) | (l_color << 4) | 0x80, al );
			undo_stat = tundo;
		}
		update_virtual_display;
		reset_virtual_display;
	}
	else
	{
		tw = window_id;
		if( tw == w2 )
		{
			call check_gone;
			if( event_macro != "")
				rm("HEXEVENTPROC /M=1/W=" + str(w1));
		}
		else
		{
			call check_gone;
		}
	}
	return(0);

check_gone:
	refresh  = false;
	if(!switch_win_id(w1))
	{
		if(switch_win_id(w2))
		{
			event_macro = "";
			window_attr = 0;
		}
	}
	else
	if(!switch_win_id(w2))
	{
		if (  switch_win_id(w1) )
		{
			event_macro = "";
			window_attr = 0;
		}
	}
	switch_win_id(tw);
	ret;


check_pass_through:
	str tstr[20];
	if ( inq_key(key1,key2,0,tstr ) )
	{
		pass_event_through;
	}
	ret;

}
