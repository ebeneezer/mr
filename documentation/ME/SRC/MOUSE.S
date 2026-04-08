macro_file MOUSE;
/*******************************************************************************
														MULTI-EDIT MACRO FILE

Name: MOUSE

Description:	Support for the mouse

MOUEVENT				- The main mouse event processor
MOUSE_MOVE			- Handles mouse invoked block marking
MOUSEINWINDOW		- Figures out if mouse is clicked inside the window or on scroll bar.
MOUSEEVENT2			- Runs the proper routines based on MOUSEINWINDOW
MOUSEFKEY				- Process mouse clicks on the FKEY labels
GOTOSCROLLBAR		- Moves to part of file corresponding to scroll bar click
HANDLESCROLLBAR - Scrolls up or down when mouse is clicked on the arrows
MOUSE_BLOCK_OP	- Move cursor to current mouse position and then copy the marked
									block.
MOUSE_SEARCH		- Search for currently marked word

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/


// Values returned by mouse_in_window
#DEFINE _in_text      0x01
#DEFINE _left         0x02
#DEFINE _top          0x04
#DEFINE _right        0x08
#DEFINE _bottom       0x10
#DEFINE _top_left     0x06
#DEFINE _top_right    0x0C
#DEFINE _bottom_left  0x12
#DEFINE _bottom_right 0x18

macro MOUEVENT TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: MOUEVENT

Description:	Traps all mouse events and determines whether to bring up the
							menu, move the cursor, invoke the function keys, etc.

Parameters:
							/M= If 1, calling of menu and block marking is disabled.
							/S=1 disable resizing of windows, and selecting other windows
							/S=2 disable selecting other windows

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int jx, twin, in_win, trefresh, tscreen_num, already_in, recent_display_time ;
	int  t_ofs, new_line, new_col, old_x, old_y, mouevent_mode, nw, no_sizing ;
  int  x1, x2, y1, y2, double_click, miw_result = 0 ;

	trefresh = refresh;
	refresh = false;
	mouevent_mode = Parse_Int('/M=', mparm_str);
	if(  mouevent_mode == 0  ) {
		if(  (mou_last_y == menu_bar_row)  ) {
			if(  (mou_last_x >= menu_bar_col) & (mou_last_x < (menu_bar_col + length(shorten_str(menu_bar_str))))  ) {
				Push_Key(key1,key2);
			}
			RM('MEUTIL3^MEMENUS /B=1');
			goto exit;
		} else if(  (menu_bar_row == 0) &
			((mou_last_y == message_row) | (mou_last_y == status_row))  ) {
			RM('MEUTIL3^MEMENUS');
			goto exit;
		}
	}

	if(  (Mou_Last_Y == FKey_Row) & (Mou_Last_X < 80)  ) {
		RM('MouseFkey');
		goto Exit;
	}
	old_x = Mou_Last_X;
	old_y = Mou_Last_Y;
	recent_display_time = 0;
	twin = cur_window;
	nw = twin;
	tscreen_num = screen_num;
	in_win = 0;
	call check_window;
	already_in = in_win;
	no_sizing = parse_int('/S=', mparm_str);
	if(  (in_win == 0) & NOT(no_sizing)  ) {
		jx = window_count + 1;
		while(  (jx > 1)  ) {
			--jx;
			switch_window( jx );
			if(  (window_attr & $81) == 0  ) {
				call check_window;
			}
		}
		if(  in_win  ) {
			switch_window( twin );
			refresh = TRUE;
		/* 	REDRAW;  */
			switch_window( nw );
			refresh = FALSE;
			goto mouse_in_window;
		}
	} else {
		goto mouse_in_window;
	}

	switch_window( twin );

	if(  (Mou_Last_Y >= min_window_row) & (Mou_Last_Y <= max_window_row)  ) {
		while(  ((Mou_Last_Status & 1) != 0) &
			((old_x == Mou_Last_X) | (old_y == Mou_Last_Y))  ) {
			Mou_Check_Status;
		}
		if(  ((Mou_Last_Status & 1) != 0)  ) {
			x1 = Old_X;
			x2 = Mou_Last_x;
			y1 = Old_Y;
			y2 = Mou_Last_Y;
			jx = 1;
			if(  (Mou_Last_Y > Old_Y)  ) {
				jx = 3;
			} else {
				y1 = Mou_Last_Y;
				y2 = Old_Y;
			}
			if(  (Mou_Last_X > Old_X)  ) {
				++jx;
			} else {
				x1 = Mou_Last_X;
				x2 = Old_X;
			}
			refresh = FALSE;
			RM('CREATEWINDOW');
			nw = cur_window;
			size_window( x1, y1, x2, y2 );
			RM('WINDOW^MOD_WIN /MM=1/MS=' + str(jx));
			RM('WINDOW^MAKEWIN /NC=1/L=1');
			switch_window(nw);
		}
	}
	goto exit;

mouse_in_window:
	if(  (Window_Attr & $40) != 0  ) {
		if(  (mou_last_x == (win_x1 + Length(window_name) +
											Length(Truncate_Path(file_name)) + 1))  ) {
			RM('WINDOW^ZOOM');
		} else if(  (Mou_Last_X <= (Win_X1 + length(window_name)))  ) {
			goto RUN_WINDOW_MENU;
		} else {
			if(  mou_double_click  ) {
				RM('WINDOW^ZOOM');
			} else {
				goto MOVE_WIN;
			}
		}
  }
  else
  {

    switch ( miw_result )
    {

      case _top :
        if(  ( no_sizing != 1 )  ) {
          if(  (mou_last_x >= (win_x1 + 2)) &
              (mou_last_x <= (win_x1 + 3 + length(window_name)))  ) {
        RUN_WINDOW_MENU:
            if(  mou_double_click  ) {
              RM('WINDOW^DELWIN');
            } else {
              refresh = false;
              RM('MEUTIL3^WINDMENU /X=' + str( mou_last_x ) + '/Y=' + str( mou_last_y ) );
            }
          } else if(  (Mou_Last_X == (win_x2 - 1))  ) {
            RM('WINDOW^ZOOM');
          } else if(  (Mou_Last_X == (win_x2 - 2))  ) {
            RM('WINDOW^ZOOM /MINIMIZE=1');
          } else {
        MOVE_WIN:
            if(mou_double_click)
              rm('WINDOW^ZOOM');
            else
              RM('WINDOW^MOD_WIN /MM=1/MS=0');
          }
        }
        break;

      case _top_left :
        if( no_sizing != 1 )
          RM('WINDOW^MOD_WIN /MM=1/MS=1');
        break;

      case _top_right :
        if( no_sizing != 1 )
          RM('WINDOW^MOD_WIN /MM=1/MS=2');
        break;

      case _bottom_right :
        if( no_sizing != 1 )
          RM('WINDOW^MOD_WIN /MM=1/MS=4');
        break;

      case _bottom_left :
        if( no_sizing != 1 )
          RM('WINDOW^MOD_WIN /MM=1/MS=3');
        break;

      case _left :
        break;

      case _bottom :
      case _right :
        rm('HandleScrollBar');
        break;

      case _in_text :
        int t_pb = persistent_blocks,
            mark_line = FALSE,
            cx2,cy2, cx = c_col, cy = c_line, obs = block_stat,
            drag = false
            ;

        persistent_blocks = TRUE;
        refresh = TRUE;
        RM('MouseInWindow /NM=1');
        if(  return_int  ) {
          cx2 = c_col;
          cy2 = c_line;
          if(  mouevent_mode != 1  ) {
            if( (block_stat != 0) && (c_line >= block_line1) && (c_line <= block_line2)) {
              if( (block_stat == 3) &&
                  (
                     ((c_line == block_line1) && (c_col < block_col1))
                  || ((c_line == block_line2) && (c_col > block_col2))
                  )
                  ) {
                goto no_block;
              }
              else if(( block_stat == 2 ) && ((block_col1 > c_col) || (block_col2 < c_col)))
              {
                goto no_block;
              }
              drag = TRUE;
              cx2 = c_col;
              cy2 = c_line;
            }
          no_block:
          }
          RM('MOUSE_MOVE /V=1/H=1/D=' + str(drag && !marking) +
              '/B=' + str( (mouevent_mode != 1) && !drag));
          if( !marking && drag && ((cx2 != c_col) || (cy2 != c_line))) {
            if( return_int )
              move_block;
          }
          else
          {
            if( (mouevent_mode != 1) && ( cx == c_col) && (cy == c_line)) {
              block_end;
              mark_pos;
              return_str = word_delimits;
              while( jx = xpos( ' ', return_str, 1 ) )
                  return_str = str_del( return_str, jx, 1 );
              while( jx = xpos( '|255', return_str, 1 ) )
                  return_str = str_del( return_str, jx, 1 );
              while( jx = xpos( '|9', return_str, 1 ) )
                  return_str = str_del( return_str, jx, 1 );
              if( xpos( cur_char, return_str, 1) )
              {
                while( xpos( cur_char, return_str, 1) && (c_col > 1) )
                    left;
                if( !xpos( cur_char, return_str, 1))
                    right;
                cx = c_col;
                forward_till_not( return_str );
              }
              else
              {
                right;
                word_left;
                cx = c_col;
                forward_till(word_delimits);
              }
              cy = c_col - 1;
              goto_mark;
              if( (obs == 3) && (cx == block_col1) && (cy == block_col2) &&
                    (c_line == block_line1) && (c_line == block_line2)) {
                block_begin;
                block_end;
              }
              else if( (obs == 1) && (block_line1 == c_line) && (block_line2 == c_line) ) {
                block_off;
              }
              else {
                block_stat = 3;
                block_line1 = block_line2 = c_line;
                block_col1 = cx;
                block_col2 = cy;
              }
              display_line;
            } else if( (cx2 == c_col) && (cy2 == c_line)) {
              persistent_blocks = t_pb;
              goto_line( c_line );
            }
            redraw;
            if( block_stat != 0 )
              set_global_int('@LAST_BLOCK_TYPE', block_stat);

          }
        }
        persistent_blocks = t_pb;
        break;
    }
  }

/*
  else if(  (Mou_Last_Y == win_y1)  ) {
	} else {
		if(  ((Mou_Last_X == win_x1) & (Mou_Last_Y <= (win_y1 + 1)))
			|
		 ((Mou_Last_Y == win_y1) & (Mou_Last_X < (win_x1 + 2)))  ) {
			if(  NOT(no_sizing)  ) {
				RM('WINDOW^MOD_WIN /MM=1/MS=1');
			}
		} else if(  ((Mou_Last_X == win_x2) & (Mou_Last_Y >= win_y2))
				|
			((Mou_Last_Y == win_y2) & (Mou_Last_X >= win_x2))  ) {
			if(  NOT(no_sizing)  ) {
			}
		} else if(  ((Mou_Last_X == win_x1) & (Mou_Last_Y == (win_y2)))  ) {
			if(  NOT(no_sizing)  ) {
			}
		} else if(  (mou_last_x == win_x2)  ) {
		} else {
		}
	}
 */
	goto exit;

check_window:
  int tmiw_result;
	if(  (window_attr & $40)  ) {
		if(  (mou_last_x >= win_x1) & (mou_last_x <= win_x2) &
			( last_update_time > recent_display_time ) & (mou_last_y == win_y1)  ) {
			goto iswin;
		}
  } else if(  (tmiw_result = mouse_in_window( mou_last_x, mou_last_y, 0)) &&
              (mou_last_y <= win_y2) &&
              ( last_update_time > recent_display_time )
            ) {
		if(  (mou_last_y == win_y2) & NOT(w_bottom_line)  ) {
			ret;
		}
 iswin:
    miw_result = tmiw_result;
		in_win = 1;
		nw = cur_window;
		recent_display_time = last_update_time;
	}
	ret;

exit:
	refresh = trefresh;
}

macro MOUSE_MOVE trans2  {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: MOUSE_MOVE

Description:	Moves the cursor or starts block marking based on mouse clicks

Parameters:

							/V= If 1, enables vertical movement.
							/H= If 1, enables horizontal movement.
							/B= If 1, enables block marking.
							/NEOF=1  No move past EOF;
							/D=1		Dragging text for move
							/D=2		Dragging text for copy

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  jx,old_refresh, old_mouse_mode, vertical, horizontal, first,
			 drag = parse_int('/D=', mparm_str), tt = system_timer, draw_count = 0,
       neof = parse_int("/NEOF=", mparm_str),
       mc_stat = 1 ;
	int  new_col, new_line, old_lo,
			 t_l_color = l_color,
			 tr = refresh, ox = mou_last_x, oy = mou_last_y,
			 interval = 0,
       block = parse_int("/B=", mparm_str),
       t_lb_color = lb_color,
       inx,iny,
			 block_cx = 0,
       ac = block_colx, al = block_linex,
       old_text_cursor = text_cursor,
       old_stream_block = stream_block_mode,

/* the following 4 variables store the window coordinates with compensation for
   the window borders being off screen */
			 w_x2 = win_x2 - (win_x2 > screen_width),
			 w_x1 = win_x1 + (win_x1 < 1),
			 w_y2 = win_y2 - (win_y2 > screen_length),
			 w_y1 = win_y1 + (win_y1 < 1) ;
	str  tstr[20], drag_char[1] ;
	first = 1;
	old_refresh = refresh;
	refresh = true;
	goto_col( c_col );
	vertical = parse_int('/V=', mparm_str);
	horizontal = parse_int('/H=', mparm_str);
	old_mouse_mode = mouse_mode;
	Mou_Check_Status;
	mou_reset;
	// mouse_mode = TRUE;
	// mou_remove_ptr;
	if ( drag == 1 ) {
		drag_char = '-';
	}
  else if( drag == 2) {
    drag_char = '+';
  }
	return_int = 1;
loop:
	Mou_Check_Status;
	if(  ((Mou_Last_Status & 3) == 0)  ) {
		return_int = 1;
		GOTO exit;
	}
	if( drag && !first  ) {
		if( (draw_count == 0) || ((system_timer - tt) >= 6) ) {
			tt = system_timer;
      if( mc_stat == 1 )
      {
        mou_remove_ptr;
        --mc_stat;
      }
			if ( draw_count < 1 ) {
        if(( drag != 3 ) || (mou_last_x > win_x1))
        {
          if( window_attr  & 0x20 )
          {
            write(drag_char,win_x1 + ((c_col - left_offset - 1) * 3) + 1,
                  c_row + win_y1,
                  0, working_color & 0x7E );
          }
          else
          {
            write(drag_char,win_x1 + (c_col - left_offset),
                  c_row + win_y1,
                  0, working_color & 0x7E );
          }
        }
				draw_count = 1;
			} else {
				display_line;
				draw_count = -1;
			}
		}
	}
  if( marking )
  {
    text_cursor = false;
    gotoxy( 0, 0 );
  }
	if(  check_key  ) {
		if(  key1 == 27  ) {
			return_int = 0;
			goto exit;
		}
		if(( key1 == 0) && (key2 == 251) && marking)
		{
			++block_stat;
			if(block_stat > 3)
				block_stat = 1;
			goto_col(c_col);
			redraw;
		}
	}
	if( (mou_last_x != ox) || (mou_last_y != oy) ) {
		if(draw_count)  {
			Display_Line;
			draw_count = 0;
		}
		if(  first  ) {
			block_cx = c_col;
			first = FALSE;
      if(  (block == 1) && (marking)  ) {
				jx = block_stat;
				block_end;
				Make_Message('Block marked.');
				set_global_int('@LAST_BLOCK_TYPE', block_stat);
			}
      else if( (block == 2) && (block_stat != 0) ) {
				refresh = false;
				jx = block_stat;
				mark_pos;
				if( (c_line < block_line1) || ((c_line == block_line1) && (c_col <= block_col1))) {
					goto_line( block_line2 );
					goto_col( block_col2 );
				}
				else {
					goto_line( block_line1 );
					goto_col( block_col1 );
				}
				str_block_begin;
				block_stat = jx;
				goto_mark;
				refresh = true;

				if(  jx == 1  ) {
          drag = 3;
          drag_char = "";
				} else if(  jx == 2  ) {
          drag = 3;
          drag_char = "";
				} else if(  (jx == 3) | (jx == 0)  ) {
          drag = 3;
          drag_char = "";
				}

			}
      else if(  block ) {
				jx = global_int('@LAST_BLOCK_TYPE');
				if(  jx == 1  ) {
					block_begin;
					tstr = 'Line';
          drag = 3;
          drag_char = "";
				} else if(  jx == 2  ) {
					tstr = 'Columnar';
          drag = 3;
          drag_char = "";
					col_block_begin;
				} else if(  (jx == 3) | (jx == 0)  ) {
        //  stream_block_mode = 1;
					tstr = 'Stream';
					str_block_begin;
          drag = 3;
          drag_char = "";
				}
        ac = c_col;
        al = c_line;
				Make_Message( 'Marking ' + tstr + ' block, release mouse button to end.' );
			}
		}
	}

//	if(  (mou_last_x > win_x1) & (mou_last_x < win_x2) &
//				(mou_last_y < win_y2) & (mou_last_y > win_y1)  ) {
// T.R. Modified to compensate for window borders being off screen
  inx = (mou_last_x > w_x1) && (mou_last_x < w_x2);
  iny = (mou_last_y < w_y2) && (mou_last_y > w_y1);
  if(  inx && iny ) {
		if(( mou_last_x != ox) || (mou_last_y != oy)) {
   //   call set_vertical;
   //   call set_horizontal;
      call set_cursor;
			refresh = tr;
		}
	} else {
    if( mc_stat == 0 )
    {
      mou_draw_ptr;
      ++mc_stat;
    }
		if( interval > 0 ) {
			if( interval > 3 )
				interval = 3;
			switch ( interval )
			{
				case 1 : delay( 35 );
				case 2 : delay( 15 );
				case 3 : delay( 5 );
			}
			interval = 0;
		}
		old_lo = left_offset;
//		if( horizontal && (mou_last_x >= win_x2) ) {
// T.R. Modified to compensate for window borders being off screen
		if( horizontal && (mou_last_x >= w_x2) ) {
      call set_cursor;
      goto_col(c_col + 3);
    /*
			while( left_offset == old_lo ) {
				right;
			}
      right;
    */
			interval = mou_last_x - win_x2 + 1;
		}
//		if( horizontal && (mou_last_x <= win_x1) ) {
// T.R. Modified to compensate for window borders being off screen
		if( horizontal && (mou_last_x <= w_x1) ) {
      call set_cursor;
      goto_col(c_col - 3);
      /*
			while( (c_col > 1) && (old_lo == left_offset) ) {
				left;
			}
      if( c_col > 1 )
      {
        left;
      }
       */
			interval = win_x1 - mou_last_x + 1;
		}
//		if( vertical && (mou_last_y >= win_y2 )) {
// T.R. Modified to compensate for window borders being off screen
		if( vertical && (mou_last_y >= w_y2 )) {
			if(!(neof  && at_eof))
				down;
			interval = mou_last_y - win_y2 + 1;
		}
//		if( vertical && (mou_last_y <= win_y1 )) {
// T.R. Modified to compensate for window borders being off screen
		if( vertical && (mou_last_y <= w_y1 )) {
			up;
			while( c_row > 1 )
				up;
			interval = win_y1 - mou_last_y + 1;
		}
    call set_cursor;

 //   call set_vertical;
 //   call set_horizontal;
	}
	ox = mou_last_x;
	oy = mou_last_y;
	if(neof && at_eof && (get_line == "")) /* changed the test for at eof to fix
																					bug in list boxes */
		up;

  if( marking && (block_stat == 3))
  {
    if( stream_block_mode )
    {
      if( mou_last_x <= win_x1 )
      {
        if( block_line2 > block_line1 )
        {
          block_col2 = c_col - 1;
          display_line;
        }
      }
    }
  }

  /*
  if( marking )
  {
    call modify_block;
  }

   */

	GOTO loop;


set_cursor:
  {

    int mx,my,cmode;
    refresh = true;
    mx = mou_last_x;
    my = mou_last_y;
    cmode = iny && vertical;
    if ( inx && horizontal )
    {
      cmode |= 2;
    }
    if ( mx <= win_x1 )
    {
      mx = win_x1 + 1;
    }
    else if ( mx >= win_x2 )
    {
      mx = win_x2 - 1;
    }
    if ( my <= win_y1 )
    {
      my = win_y1 + 1;
    }
    else if ( my >= win_y2 )
    {
      my = win_y2 - 1;
    }
    mouse_in_window( mx, my, cmode );
  }
  ret;


exit:
  if(  NOT(first) && block && (block_stat != 0)  ) {
		block_end;
  /*
    if( block_stat == 3 )
    {
      if((( c_col >= ac ) && ( c_line == al))
          || (c_line > al))
      {
        goto_col( block_col2 + 1 );
      }
    }
  */
		if( tstr != '')
			Make_message('Block marked.  Default mouse marking mode is ' + tstr + '.');
		set_global_int('@LAST_BLOCK_TYPE', block_stat);
	}
  if( mc_stat == 0 )
  {
    mou_draw_ptr;
  }

  text_cursor = old_text_cursor;
  stream_block_mode = old_stream_block;
	mouse_mode = old_mouse_mode;
	refresh = old_refresh;
 // Mou_Draw_Ptr;
}

macro MOUSEINWINDOW trans2  {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: MOUSEINWINDOW

Description:	Handles the mouse when clicked inside the window

RETURNS:    RETURN_INT = 1 if mouse was inside window,
						RETURN_INT = 0 if mouse was not inside window.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

  /*
  int  new_col, new_line,
			 t_l_color = l_color,
			 tr = refresh,
			 t_lb_color = lb_color ;
  */

/*
	if(  (mou_last_x > win_x1) & (mou_last_x < win_x2) &
        (mou_last_y < win_y2) & (mou_last_y > win_y1)  ) {     */
  if( mouse_in_window( mou_last_x, mou_last_y, false ) == 1 ) {
    if(marking && parse_int("/NM=", mparm_str))
    {
      set_global_int('@LAST_BLOCK_TYPE', block_stat);
      block_end;
    }
    mouse_in_window( mou_last_x, mou_last_y, 3 );
    /*
		l_color = 0;
		lb_color = 0;
		refresh = FALSE;
		goto_v_col( v_col );
		new_col = (v_col - (wherex - win_x1)) + (Mou_Last_X - win_x1);
		new_line = (c_line - c_row) + (Mou_Last_Y - win_y1);
		if(  c_line < new_line  ) {
			while(  c_line < new_line  ) {
				DOWN;
			}
		} else {
			while(  c_line > new_line  ) {
				UP;
			}
		}
		l_color = t_l_color;
		lb_color = t_lb_color;
		refresh = true;
		goto_v_col( new_col );
		refresh = tr;
     */
		return_int = 1;
	} else {
		return_int = 0;
	}

}

macro MOUSEEVENT2 trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: MOUSEEVENT2

Description:	Determines whether to move the text cursor or process the scroll
							bar.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int t_pb = persistent_blocks,
			cx2,cy2,
			drag = false
			;
	persistent_blocks = TRUE;
	if( (mou_last_x > win_x2) || (mou_last_x < win_x1) || (mou_last_y > win_y2)
		|| (mou_last_y < win_y1 ) ) {
			rm("MOUEVENT /M=1");
		}
	RM('MouseInWindow');
	if(  return_int  ) {
		cx2 = c_col;
		cy2 = c_line;
	 	if( (block_stat != 0) && (c_line >= block_line1) && (c_line <= block_line2)) {
			if( (block_stat == 3) &&
					(
							((c_line == block_line1) && (c_col < block_col1))
					|| ((c_line == block_line2) && (c_col > block_col2))
					)
					) {
				goto no_block;
			}
			else if(( block_stat == 2 ) && ((block_col1 > c_col) || (block_col2 < c_col)))
			{
				goto no_block;
			}
			drag = 2;
			cx2 = c_col;
			cy2 = c_line;
		}
	no_block:
		if ( !drag ) {
			persistent_blocks = t_pb;
			goto_line( c_line );
		}
		RM('Mouse_Move /V=1/H=1/D=' + str(drag));
		if( drag && ((cx2 != c_col) || (cy2 != c_line))) {
			if( return_int )
				copy_block;
		}
	} else {
		persistent_blocks = t_pb;
		RM('GotoScrollBar');
	}
	persistent_blocks = t_pb;
}

macro MOUSEFKEY trans2  {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: MOUSEFKEY

Description:	Handles mouse clicks on the function key line.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  Shift_Status, key_num, shift_add ;

	if(  Mou_Last_Y == FKey_Row  ) {
		R_AX = $0200;
		INTR($16);
		Shift_Status = R_AX & $0F;
		shift_add = 0;
		if(  (Mou_Last_X % 8) == 0  ) {
			goto exit;
		}
		key_num = (Mou_Last_X / 8) + 1;
		if(  (Shift_Status == 0)  ) { /* Normal */
			Shift_Add = 58;
		} else if(  (Shift_Status == 4)  ) { /* Ctrl */
			Shift_Add = 93;
		} else if(  (Shift_Status > 0) & (Shift_Status < 4)  ) { /* shifted */
			Shift_Add = 83;
		} else if(  (Shift_Status == 8)  ) {
			Shift_Add = 103;
		} else {
			Goto EXIT;
		}
		Push_Key(0,Shift_Add + Key_Num);
	}
EXIT:
}

macro GOTOSCROLLBAR trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: GOTOSCROLLBAR

Description:	Moves to the line in the file corresponding to the scroll bar.

Parameters:
							/L=	Overrides the total lines in the file.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  jz, jx, jy, tr ;
	tr = refresh;
	refresh = false;
	if(  Mou_Last_Y == Win_Y2  ) {
		jz = 2048;
		if(  (Mou_Last_X < (Win_X2 - 1)) &
			(Mou_Last_X > (Win_X1 + 1))  ) {
			if(  (Mou_Last_X == (Win_X1 + 2))  ) {
				goto_col(1);
			} else if(  (Mou_Last_X == (Win_X2 - 2))  ) {
				goto_col( jz );
			} else {
				jy = win_x2 - win_x1 - 3;
				jx = ((jz - jy)) * 100;
				jx = jx / (jy - 2);
				jy = mou_last_x - (win_x1 + 1);
				jx = jx * jy;
				if(  (jx % 100 ) != 0  ) {
					jx = (jx / 100) + 1;
				} else {
					jx = jx / 100;
				}
				goto_col(jx);
				left_offset = jx;
			}
		}

	} else {
		jz = parse_int('/L=', mparm_str);
		jx = c_col;
		if(  (Mou_Last_X == Win_X2) & (Mou_Last_Y < (Win_Y2 - 1)) &
			(Mou_Last_Y > (Win_Y1 + 1))  ) {
			if(  (window_attr & $08)  ) {
				goto exit;
			}
			if(  (Mou_Last_Y == (Win_Y1 + 2))  ) {
				tof;
				goto_col(jx);
			} else if(  (Mou_Last_Y == (Win_Y2 - 2))  ) {
				eof;
				goto_col(jx);
			} else {
				if(  jz == 0  ) {
					eof;
					jz = c_line;
					goto_col( jx );
				}
				while(  (c_row > 1) & (c_line > 1)  ) {
					up;
				}

				jy = win_y2 - win_y1 - 1;
				jx = ((jz - jy)) * 1000;
				jx = jx / (jy - 2);
				jy = mou_last_y - (win_y1 + 1);
				jx = jx * jy;
				if(  (jx % 1000 ) != 0  ) {
					jx = (jx / 1000) + 1;
				} else {
					jx = jx / 1000;
				}

				goto_line(jx);
			}
		}
	}
exit:
	refresh = tr;
	return_int = jz;
}

macro HANDLESCROLLBAR trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: HANDLESCROLLBAR

Description:	Scrolls up or down when mouse is clicked or held down on the
							arrows of the scroll bar.


Parameters:
		/EOF=1		Limit movement to EOF.
		/M=1
		/HI=1			Highlight current line

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int jx, jy, old_x, old_y, eof_limit, direction_result, m,
			hi = parse_int('/HI=', mparm_str ),
			scroll_delay = global_int("@MOU_SCRL_BAR_DELAY");  // allows user setting of repeat rate

	if (scroll_delay < 0)
		scroll_delay = 0;

	eof_limit = parse_int('/EOF=', mparm_str );
	m = parse_int('/M=', mparm_str);
	old_x = Mou_Last_X;
	old_y = Mou_Last_Y;
	direction_result = 0;
	if(  (mou_last_x == win_x2)  ) {
		if(  (window_attr & $08)  ) {
			goto exit;
		}
		if(  (mou_last_y == (win_y2 - 1))  ) {
			call set_limit;
			if(  M == 0  ) {
				refresh = false;
				jx = c_row;
				while(  c_row < ((win_y2 - win_y1 - 1))  ) {
					down;
				}
			}
				refresh = TRUE;
			scroll_up:
				call lolight;
				down;
				call check_eof;
				call highlight;
				CALL Mouse_Repeat_Loop;
				if(  return_int == 1  ) {
					Goto Scroll_Up;
				}
			if(  M == 0  ) {
				refresh = false;
				while(  c_row > jx  ) {
					up;
				}
				refresh = TRUE;
			}
				direction_result = 1;
		} else if(  (mou_last_y == (win_y1 + 1))  ) {
			call set_limit;
			if(  M == 0  ) {
				refresh = false;
				jx = c_row;
				while(  c_row > 1  ) {
					up;
				}
			}
				refresh = TRUE;
			scroll_down:
				call lolight;
				up;
				call highlight;
				CALL Mouse_Repeat_Loop;
				if(  return_int == 1  ) {
					Goto Scroll_Down;
				}
			if(  M == 0  ) {
				refresh = false;
				while(  c_row < jx  ) {
					down;
				}
				refresh = TRUE;
			}
				direction_result = 2;
		} else if(  (Mou_Last_y > win_y1) & (Mou_Last_y < win_y2)  ) {
			call set_limit;
				refresh = TRUE;
			page_loop:
				jx = Mou_Last_Y - Win_Y1 - 1;
				if(  jx > cur_scroll_pos  ) {
					call lolight;
					page_down;
					call check_eof;
					call highlight;
					direction_result = 1;
				} else if(  jx < cur_scroll_pos  ) {
					call lolight;
					page_up;
					call highlight;
					direction_result = 2;
				} else if(  jx == cur_scroll_pos  ) {
					goto drag_scroll_pos;
				}
				CALL Mouse_Repeat_Loop;
				if(  return_int == 1  ) {
					Goto page_loop;
				}
		}
	} else if(  mou_last_y == win_y2  ) {
		if(  (mou_last_x == (win_x2 - 1))  ) {
			call set_limit;
				refresh = TRUE;
			scroll_right:
				call lolight;
        if(  (left_offset < 2046) && (c_col < 2048)  ) {
					left_offset = left_offset + 1;
					right;
				}
				update_window;
				call check_eof;
				call highlight;
				CALL Mouse_Repeat_Loop;
				if(  return_int == 1  ) {
					Goto Scroll_right;
				}
			direction_result = 3;
		} else if(  (mou_last_x == (win_x1 + 1))  ) {
			refresh = TRUE;
			call set_limit;
			scroll_left:
				call lolight;
				if(  left_offset > 0  ) {
					left_offset = left_offset - 1;
				}
				if(  c_col > 1  ) {
					left;
				}
				update_window;
				call highlight;
				CALL Mouse_Repeat_Loop;
				if(  return_int == 1  ) {
					Goto Scroll_left;
				}
			direction_result = 4;
		} else if(  (Mou_Last_x > win_x1) & (Mou_Last_x < win_x2)  ) {
				refresh = TRUE;
			call set_limit;
			page_loop2:
				jx = Mou_Last_X - Win_X1 - 1;
				if(  jx > cur_h_scroll_pos  ) {
					call lolight;
					call page_right;
					call highlight;
					direction_result = 3;
				} else if(  jx < cur_H_scroll_pos  ) {
					call lolight;
					call page_left;
					call highlight;
					direction_result = 4;
				} else if(  jx == cur_h_scroll_pos  ) {
					goto drag_H_scroll_pos;
				}
				CALL Mouse_Repeat_Loop;
				if(  return_int == 1  ) {
					Goto page_loop2;
				}
		}
	}
	GOTO EXIT;
highlight:
	if(  hi  ) {
		draw_attr( win_x1 + 1, wherey, h_color, (win_x2 - win_x1 - 1) );
	}
	ret;

lolight:
	if(  hi  ) {
		draw_attr( win_x1 + 1, wherey, t_color, (win_x2 - win_x1 - 1) );
	}
	ret;

check_eof:
	if(  eof_limit  ) {
		if(  at_eof  ) {
			jy = c_col;
			goto_col(1);
			while(  (at_eof) & (c_line > 1)  ) {
				up;
			}
			goto_col(jy);
		}
	}
	ret;

Mouse_Repeat_Loop:
		Mou_Repeat = TRUE;
		du2:
			Mou_Check_Status;
			if(  ((Mou_Last_Status & 1) != 0) & (old_x == Mou_Last_X) &
				(old_y == Mou_Last_Y)  ) {
				if(  check_Key  ) {
					if(  (key2 == 250) & (key1 == 0)  ) {
						delay(scroll_delay);
						return_int = 1;
						RET;
					} else {
						push_key( key1, key2 );
					}
				} else {
					GOTO du2;
				}
			}
		return_int = 0;
		Mou_Repeat = FALSE;
		RET;

page_right:
		jx = wherex;
		left_offset = left_offset + ((win_x2 - win_x1) / 2);
		goto_col( left_offset + jx - win_x1 );
		update_window;
	 ret;

page_left:
		jx = wherex;
		jy = ((win_x2 - win_x1) / 2);
		if(  left_offset > jy  ) {
			left_offset = left_offset - jy;
		} else {
			left_offset = 0;
		}
		goto_col( left_offset + jx - win_x1 );
		update_window;
	 ret;

drag_scroll_pos:
		return_int = parse_int('/L=', mparm_str);
		Mou_Set_Limits(win_x2,win_y1 + 2,win_x2, win_y2 - 2);
		Mou_Repeat = FALSE;
		call highlight;
		du3:
			Mou_Check_Status;
			if(  ((Mou_Last_Status & 1) != 0)  ) {
				if(  old_y != mou_last_y  ) {
					RM('GOTOSCROLLBAR /L=' + str(return_int));
					REFRESH = TRUE;
					update_window;
					call highlight;
				}
				old_y = mou_last_y;
				goto du3;
			}
		direction_result = 1;
		goto exit;

drag_h_scroll_pos:
		Mou_Set_Limits(win_x1 + 2,win_y2,win_x2 - 2, win_y2);
		Mou_Repeat = FALSE;
		call highlight;
		du3x:
			Mou_Check_Status;
			if(  ((Mou_Last_Status & 1) != 0)  ) {
				if(  old_x != mou_last_x  ) {
					RM('GOTOSCROLLBAR');
					REFRESH = TRUE;
					update_window;
					call highlight;
				}
				old_x = mou_last_x;
				goto du3x;
			}
		direction_result = 1;
		goto exit;

set_limit:
	 Mou_Set_Limits(mou_last_x,mou_last_y,mou_last_x,mou_last_y);
	 ret;
exit:
	 Mou_Set_Limits(1,1,screen_width, screen_length);
	 return_int = direction_result;
}

/********************************MULTI-EDIT MACRO******************************

Name:  MOUSE_BLOCK_OP

Description:  Move cursor to current mouse position and then copy the marked
							block.

Parameters:   /BT=0		Copy block
									1		Move block
									2   Delete block

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
macro MOUSE_BLOCK_OP trans2 {
	int tpb = persistent_blocks;
	persistent_blocks = TRUE;
	RM('MouseInWindow');
	if(  return_int  ) {
		RM('Meutil2^BlockOp /BT=' + str(parse_int('/BT=',mparm_str)));
	}
	persistent_blocks = tpb;
} /* MOUSE_BLOCK_OP */

/********************************MULTI-EDIT MACRO******************************

Name:  MOUSE_SEARCH

Description:  Search for currently marked word

Parameters:   /D=1		search backwards

*******************************************************************************/
macro MOUSE_SEARCH trans2 {
	str save_switches;
	int save_search_mode;
	int pb = persistent_blocks;

	persistent_blocks = TRUE;
	if(  (Block_Stat > 0) && (block_line1 == block_line2)  ) {
		refresh = FALSE;
		mark_pos;
		goto_line(block_line1);
		set_global_str('SEARCH_STR', copy(get_line, block_col1, block_col2 - block_col1 + 1));
		goto_mark;
		save_search_mode = global_int('SEARCH_MODE');
		Set_Global_Int('SEARCH_MODE', 0);  // set for search only (no replace)
		save_switches = global_str('SWITCHES');  // save search option switches
		if(  parse_int('/D=', mparm_str)  ) {
			set_global_str('SWITCHES', 'IBX');
			if( (block_line1 == c_line) && (c_col > block_col1)) {
				goto_col(block_col1);
			}
			left;
		} else {
			set_global_str('SWITCHES', 'IX');
			right;
		}
		Set_Global_Int('REPSEARCH', 1 );
		RM('S_AND_R');
		Set_Global_Int('REPSEARCH', 0 );
		Set_Global_Str('SWITCHES', save_switches);  // restore search option switches
		Set_Global_Int('SEARCH_MODE', save_search_mode); // restore search mode
	} else {
		RM('MOUSEINWINDOW');
		if(  return_int  ) {
			mark_pos;
			right;
			word_left;
			str_block_begin;
			get_word(word_delimits);
			block_end;
			goto_mark;
		}
	}
	persistent_blocks = pb;
} /* MOUSE_SEARCH */


/****************************Multi-Edit Macro********************************

 NAME:         CUA_Shft_Button

 DESCRIPTION:  CUA compatible Shift-Left_Mouse_Button routine

 PARAMETERS:

 RETURNS:

*****************************11-25-91 01:41pm*******************************/
macro CUA_Shft_Button
{
	int c = c_col, l = c_line,
		  pb = persistent_blocks, b = 2;

	persistent_blocks = TRUE;

	block_end;
	rm('MouseInWindow');
	if( return_int ) {
		if( block_stat != 0 ) {
			if( block_stat == 1) {
				if( block_line1 < c_line )
						block_line2 = c_line;
				else
						block_line1 = c_line;
			}
			else if (block_stat == 3) {
				if( (c_line < block_line1) || ((c_line == block_line1) && (c_col < block_col1))) {
					block_line1 = c_line;
					block_col1 = c_col;
				}
				else {
					block_line2 = c_line;
					block_col2 = c_col;
				}
			} else {
				if( c_line < block_line1 ) {
					block_line1 = c_line;
					if( c_col >= block_col2) {
						block_col2 = c_col;
					}
					else {
						block_col1 = c_col;
					}
				} else {
					if( c_col < block_col1 ) {
						if( c_line > block_line2 )
								block_line2 = c_line;
						else
								block_line1 = c_line;
						block_col1 = c_col;
					} else {
						block_line2 = c_line;
						block_col2 = c_col;
					}
				}
			}
		}
		else {
			if( (c_line < l) || ((c_line == l) && (c_col < c))) {
				block_line1 = c_line;
				block_line2 = l;
				block_col1 = c_col;
				block_col2 = c;
			}
			else {
				block_line2 = c_line;
				block_line1 = l;
				block_col2 = c_col;
				block_col1 = c;
			}
			block_stat = 3;
		}
		redraw;
		rm('Mouse_Move /H=1/V=1/B=2');
	}
	persistent_blocks = pb;
}
