//+++keyword+++ "@(#)%v %n, %f, %w"
// "@(#)5 CDIFF.S, 7-Aug-93,23:53:28, LDH"

macro_file cdiff;
/******************************************************************************
                             Multi-Edit Macro File
                               14-Mar-92  17:53

  Colorized Difference Macro

                 Copyright (C) 1992 by Small Systems Support.
*********************************************************************(ldh)** */

global {
  int @cdif_acolor;
  int @cdif_dcolor;
}

macro cdiff trans
{
/******************************************************************************
                               Multi-Edit Macro
                               10-Feb-93  21:58

  Macro   : cdiff (Color Difference)
  Function: Compares the file in the current window to a specified file and
            display a colorized difference file.
  Syntax  : RM('cdiff /F=str/DF=str');
  Entry   : /F=str  Filename of file to compare with current file.
            /DF=str Filename of the Differences
            /D=int  0 - show color display, 1 - show diff file
  Exit    : None.

                Copyright (C) 1992-93 by Small Systems Support.
***********************************************************************(ldh)**/

//+++keyword+++ "@(#)%v %n, %f, %w"
  str rev_id = "@(#)5 CDIFF.S, 7-Aug-93,23:53:28, LDH";

  int old_win = window_id,
      cpy_win,
      dif_win,
      lst_win,
      lcnt,
      ln,
      cnt,
      tline,
      diff = parse_int('/D=', mparm_str),
      use_dif = TRUE,
      sudo = undo_stat,
      srf = refresh,
      sr = reg_exp_stat,
      si = insert_mode;

  str org_file = file_name,
      fname[120] = parse_str('/F=', mparm_str),
      CDif_name[120] = parse_str('/DF=', mparm_str),
      lst_name[120],
      cmd[2],
      tmp[20];

  if (@cdif_acolor == 0)
    @cdif_acolor = 32;

  if (@cdif_dcolor == 0)
    @cdif_dcolor = 64;

  working;
  insert_mode = TRUE;
  reg_exp_stat = TRUE;
  refresh = FALSE;
  undo_stat = FALSE;

  if (CDif_name == '') {
    CDif_name = user_path + 'CDIF.TMP';
    use_dif = FALSE;
  }

  if (switch_file(CDif_name))
    erase_window;
  else {
    switch_window(window_count);
    create_window;
  }
  window_attr = $81;
  dif_win = window_id;
  cpy_win = cur_window;

  lst_name = fexpand('CDIF_TMP.' + get_extension(org_file));
  if (switch_file(lst_name)) {
    erase_window;
  }
  else {
    switch_window(window_count);
    create_window;
  }

  lst_win = window_id;
  if (fname == '') {
    return_str = org_file;
    rm('meutil1^file_prompt /T=Filename to Compare');
    if (return_int == 0) {
      delete_window;
      goto Err_Exit;
    }
    fname = return_str;
  }
  load_file(fname);
  if (error_level) {
    goto Err_Exit;
  }
  read_only = FALSE;
  file_name = lst_name;

  if (!use_dif) {
    make_message('Doing Compare...');
    return_str = me_path + 'vcmpr.exe ' + fname + ' ' + org_file + ' ' + CDif_name + ' insdel';
    RM('MEUTIL1^EXEC /RED=NUL');

    return_int = exit_code;
    if (return_int) {
      if (return_int == 1)
        make_message('Files the same.');
      goto Exit;
    }
  }

  switch_win_id(dif_win);
  load_file(CDif_name);
  if (error_level) {
    goto Err_Exit;
  }

  if (!diff) {
    make_message('Colorizing Difference File...');
    goto_col(1);
    lcnt = 0;
    while (!at_eof) {
      cmd = get_word(' ');
      right;
      search_fwd('[~ ]', 1);
      val(ln, get_word(' '));
      search_fwd('[~ ]', 1);
      val(cnt, get_word(' '));

      if (cmd == '.D') {
//         make_message('D Command ' + str(ln) + ' ' + str(cnt));
        switch_win_id(lst_win);
        tline = ln + lcnt;
        goto_line(tline);
        block_begin;
        goto_line(tline + cnt - 1);
        rm('colorize /C=1/CN=' + str(@cdif_dcolor));         // Mark lines DEL
      }
      else if (cmd == '.I') {
        down;
//         make_message('I Command ' + str(ln) + ' ' + str(cnt));
        block_begin;
        goto_line(c_line + cnt - 1);
        switch_win_id(lst_win);
        tline = ln + lcnt;
        goto_line(tline + 1);
        window_copy(cpy_win);
        goto_line(block_line2);
        rm('colorize /C=1/CN=' + str(@cdif_acolor));              // Mark lines ADD
        lcnt = lcnt + cnt;
      }
      else
        make_message('Invalid Command');
      down;
      switch_win_id(dif_win);
      down;
      block_off;
      goto_col(1);
    }

    switch_win_id(lst_win);
    file_changed = FALSE;
    Read_Only = TRUE;
    rm('EXTSETUP');
    l_color = 0;
    TOF;
    while (line_attr == 0) {
      down;
    }
    if (c_line != 1) {
      int tmp, value;

      tmp = c_line;
      value = ((win_y2 - win_y1) / 2);
      while (c_row > value) {
        up;
      }
      while (c_row < value) {
        down;
      }
      goto_line(tmp);
    }
    key_to_window(<F1>, 'mehelp /F=VCS/LK=OPTS_CDIF');
    key_to_window(<ALTSHFTA>, 'cdif_srch');
    key_to_window(<ALTSHFTS>, 'cdif_srch /C=1');
    key_to_window(<ALTSHFTD>, 'cdif_srch /C=2');
    key_to_window(<ALTSHFTF>, 'cdif_srch /C=3');
    key_to_window(<ALTSHFTX>, 'cdif_srch /C=4');
    key_to_window(<ALTSHFTE>, 'cdif_srch /C=5');
    key_to_window(<ALTSHFTL>, 'cdif_kill');
    key_to_window(<ALTSHFTK>, 'cdif_kill /C=1');
    key_to_window(<ALTSHFTN>, 'cdif_kill /C=2');
    key_to_window(<ALTSHFTO>, 'cdif_kill /C=3');
    undo_stat = TRUE;
    refresh = TRUE;
    redraw;
    make_message('<ESC> to exit viewing change file');
    while (1) {
      if (check_key) {
        if (key1 == 27) {
          break;
        }
        pass_key(key1, key2);
      }
    }
  }
  else {
    switch_win_id(dif_win);
    refresh = TRUE;
    Read_Only = TRUE;
    RM('UserIn^EditWindow /W=77/L=' + str(screen_length - 10) +
                          '/H=VCS^VCS_LIST/T=Differences');
  }
  make_message('');
  Read_Only = FALSE;
  goto Exit;

Err_Exit:
  return_int = 2;

Exit:

  if (switch_win_id(dif_win)) {
    tof;
    window_attr = $00;
    file_name = truncate_extension(fname) + ".DIF";
    delete_window;
  }
  set_file_attr(CDif_name, 0);
  del_file(CDif_name);


  if (switch_win_id(lst_win)) {
    file_name = fname;
    delete_window;
  }

  switch_win_id(old_win);

  undo_stat = sudo;
  refresh = srf;
  reg_exp_stat = sr;
  insert_mode = si;

}  // cdiff

macro colorize trans
{
/****************************************************************************
  Sets the line color for every line in a block to a user specified color.
   If no block is marked then only the current line is colored.

  Parameters:  /F= - If non-zero every line in the file will be changed.
               /C= - 1 To colorize changed lines without confirmation.
                   - 2 To not colorize changed lines without confirmation.
               /CN=  Color Number

            (C) Copyright 1992 by American Cybernetics, Inc.
*****************************************************************************/
  int i = Block_Line1,
      End = Block_Line2,
      Unchange = Parse_Int('/C=',MParm_Str),
      Color = parse_int('/CN=', mparm_str),
      OldRefresh = Refresh;

  str Type = 'BLOCK';

  Mark_Pos;
  if (Parse_Int('/F=',MParm_Str))
  {
    TOF;
    Type = 'FILE';
    End = 0x7FFFFFFF;
  }
  else if (!Block_Stat)
  {
    Type = 'LINE';
    End = i;
  }
  else
    Goto_Line(i);
  if (!color) {
    if ((return_int = global_int('@CColorLast')) == 0)
      return_int = t_color;
    rm('Setup^ColorChart /T=COLORIZE CURRENT ' + Type);
    if ((color = return_int) == -1)
      goto exit;
  }
  set_global_int('@CColorLast', color);
  block_off;
  refresh = false;
  while (!At_EOF && (i++ <= End)) {
    if (!unchange && line_changed ) {
      rm('Verify /T=Changed lines detected.  Do you want to colorize them also.');
      if (return_int) {
        unchange = 1;
        line_changed = 0;
      }
      else
        unchange = 2;
    }
    else if (unchange != 2)
      line_changed = 0;
    line_attr = color;
    down;
  }
  refresh = oldrefresh;

Exit:
  Goto_Mark;
}  // colorize

macro cdif_srch trans
{
/******************************************************************************
                               Multi-Edit Macro
                               10-Feb-93  21:58

  Macro   : cdif_srch
  Function: Search for added or deleted lines
  Syntax  : RM('cdif_srch /C=cmd');
  Entry   : /C=cmd  0 - Search fwd for Added lines.
                    1 - Search bwd for Added lines.
                    2 - Search fwd for Deleted lines.
                    3 - Search bwd for Deleted lines.
                    4 - Search fwd for any changed lines.
                    5 - Search bwd for any changed lines.
  Exit    : Line positioned.

                Copyright (C) 1992-93 by Small Systems Support.
***********************************************************************(ldh)**/

  int cmd = parse_int('/C=', mparm_str),
      srf = refresh,
      color,
      found = FALSE,
      cur_la = line_attr;

  refresh = FALSE;
  working;
  mark_pos;
  switch (cmd) {
    case 0:
      while ((line_attr == cur_la) && !at_eof)
        down;
      while ((line_attr != @cdif_acolor) && !at_eof)
        down;
      if (line_attr == @cdif_acolor)
        found = TRUE;
      break;

    case 1:
      while ((line_attr == cur_la) && (c_line != 1))
        up;
      while ((line_attr != @cdif_acolor) && (c_line != 1))
        up;
      while ((line_attr == @cdif_acolor) && (c_line != 1))
        up;
      if (line_attr != @cdif_acolor)
        down;
      if (line_attr == @cdif_acolor)
        found = TRUE;
      break;

    case 2:
      while ((line_attr == cur_la) && !at_eof)
        down;
      while ((line_attr != @cdif_dcolor) && !at_eof)
        down;
      if (line_attr == @cdif_dcolor)
        found = TRUE;
      break;

    case 3:
      while ((line_attr == cur_la) && (c_line != 1))
        up;
      while ((line_attr != @cdif_dcolor) && (c_line != 1))
        up;
      while ((line_attr == @cdif_dcolor) && (c_line != 1))
        up;
      if (line_attr != @cdif_dcolor)
        down;
      if (line_attr == @cdif_dcolor)
        found = TRUE;
      break;

    case 4:
      while ((line_attr == cur_la) && !at_eof)
        down;
      while ((line_attr == 0) && !at_eof)
        down;
      if ((line_attr == @cdif_acolor) || (line_attr == @cdif_dcolor))
        found = TRUE;
      break;

    case 5:
      while ((line_attr == cur_la) && (c_line != 1))
        up;
      while ((line_attr == 0) && (c_line != 1))
        up;

      cur_la = line_attr;
      while ((line_attr == cur_la) && (c_line != 1))
        up;
      if (line_attr != cur_la)
        down;
      if ((line_attr == @cdif_acolor) || (line_attr == @cdif_dcolor))
        found = TRUE;
      break;
  }

  if (!found)
    goto_mark;
  else {
    pop_mark;
    if (c_line != 1) {
      int tmp, value;

      tmp = c_line;
      value = ((win_y2 - win_y1) / 2);
      while (c_row > value) {
        up;
      }
      while (c_row < value) {
        down;
      }
      goto_line(tmp);
    }
  }

  redraw;

  refresh = srf;

} // cdif_srch

macro cdif_kill trans
{
/******************************************************************************
                               Multi-Edit Macro
                               09-Mar-93  22:29

  Macro   : cdif_kill
  Function: Kills the added or deleted lines the cursor is
  Syntax  : RM('cdif_kill /C=cmd');
  Entry   : /C=cmd  0 - Delete one line.
                    1 - Delete to end of block (add, delete) or one line.
                    2 - Delete all add lines (New).
                    3 - Delete all delete line (Old).
  Exit    : Lines deleted.

                 Copyright (C) 1993 by Small Systems Support.
***********************************************************************(ldh)**/

  int srf = refresh,
      cmd = parse_int('/C=', mparm_str),
      cur_la = line_attr;

  read_only = false;
  refresh = FALSE;
  working;
  push_undo;
  switch (cmd) {
    case 0:
      del_line;
      break;
    case 1:
      if ((cur_la == @cdif_acolor) || (cur_la == @cdif_dcolor)) {
        while ((line_attr == cur_la) && !at_eof)
          del_line;
      }
      else
        del_line;
      break;
    case 2:
      mark_pos;
      tof;
      while (!at_eof) {
        while ((line_attr != @cdif_acolor) && !at_eof)
          down;
        while ((line_attr == @cdif_acolor) && !at_eof) {
          del_line;
        }
      }
      goto_mark;
      break;
    case 3:
      mark_pos;
      tof;
      while (!at_eof) {
        while ((line_attr != @cdif_dcolor) && !at_eof)
          down;
        while ((line_attr == @cdif_dcolor) && !at_eof) {
          del_line;
        }
      }
      goto_mark;
      break;
  }
  pop_undo;
  redraw;
  refresh = srf;
} // cdif_srch

macro cdif_save trans
{
/******************************************************************************
                               Multi-Edit Macro
                               10-Feb-93  23:19

  Macro   : cdif_save
  Function: Saves and compiles the changes to the user's STARTUP.S file
            for the CDIFF colors.
  Syntax  : rm('cdif_save');
  Entry   : None.
  Exit    : Changes written to STARTUP.S and then compiled.

                Copyright (C) 1990-93 by Small Systems Support.
***********************************************************************(ldh)**/

  str file_spec,
      e_f_name;

  int sr = reg_exp_stat,
      si = insert_mode,
      srf = refresh,
      Old_Win = window_id,
      Tmp_Win,
      Err_Win;

  reg_exp_stat = FALSE;
  insert_mode = TRUE;
  refresh = FALSE;

	// Mod to handle correct placement of .TMP files
	rm("TMP_FILE_NAME /FN=SETER.TMP");
	e_f_name = return_str;

  switch_window(window_count);
  create_window;
  tmp_win = window_id;
	// Mod to handle correct placement of .TMP files
  rm("TMP_FILE_NAME /FN=!!CDIF!!.TMP");
//  file_name = '!!CDIF!!.TMP';
	file_name = return_str;
  file_changed = 0;
  return_str = 'STARTUP.S';
  error_level = 0;
  rm('ldfiles');
  if (error_level != 0) {
    error_level = 0;
    return_str = me_path + 'SRC\STARTUP.S';
    rm('LdFiles');
    if (error_level != 0) {
      rm('MEERROR^MessageBox /B=2/M=Unable to locate STARTUP.S. Installation not complete.');
      error_level = 0;
      goto exit;
    }
  }

  tof;
  make_message('Saving CDIFF colors in "STARTUP.S"...');

  if (search_fwd('**** CDIFF setup START ****', 0)) {
		insert_mode = FALSE;
		goto_col(1);
		down;
    put_line('|9set_global_int(''@cdif_acolor'', ' + str(global_int('@cdif_acolor')) + ');');
    down;
    put_line('|9set_global_int(''@cdif_dcolor'', ' + str(global_int('@cdif_dcolor')) + ');');

//     block_begin;
//     if (search_fwd('**** CDIFF setup END ****', 0)) {
//       block_end;
//       delete_block;
//     }
//     block_end;
  }
	else {
  	eof;
  	if (search_bwd('}', 0)) {
    	goto_col(1);
    	cr;
    	up;
    	put_line('//**** CDIFF setup START ****');
    	down;
    	put_line('|9set_global_int(''@cdif_acolor'', ' + str(global_int('@cdif_acolor')) + ');');
    	down;
    	put_line('|9set_global_int(''@cdif_dcolor'', ' + str(global_int('@cdif_dcolor')) + ');');
    	down;
    	put_line('//**** CDIFF setup END ****');
    	down;
    	down;
    	put_line('}  // Startup');
		}
  	else {
    	make_message('Corrupt STARTUP.S.');
    	goto Exit;
  	}
	}

  save_file;

  return_int = TRUE;
//  del_file(me_path + user_id + 'SETER.TMP');
	del_file(e_f_name);
  error_level = 0;
  return_str = me_path + 'CMAC -P' + me_path + 'MAC ' + file_name;

//  rm('MEUTIL1^EXEC /RED=' + me_path + user_id + 'SETER.TMP');
  rm('MEUTIL1^EXEC /RED=' + e_f_name);
  if (error_level !=  0) {
    rm('MEERROR^MessageBox /B=2/M=Error occurred compiling STARTUP.S.');
    return_int = FALSE;
    goto Exit;
  }
  switch_window(Window_Count);
  create_window;
  err_win = window_id;
//  load_file(me_path + user_id + 'SETER.TMP');
	load_file(e_f_name);
  return_int = 1;
  if (error_level == 0) {
    if (search_fwd('ERROR',0)) {
      rm('MEERROR^MessageBox /B=2/M=Error occurred compiling STARTUP.S.');
      return_int = FALSE;
      goto Exit;
    }
  }
//  del_file(me_path + user_id + 'SETER.TMP');
	del_file(e_f_name);
  if (Switch_win_id(Err_Win))
    delete_window;
  make_message('CDIFF colors installed in STARTUP.S');

Exit:
  if (switch_win_id(tmp_win))
    delete_window;
  switch_win_id(old_win);
  refresh = srf;
  insert_mode = si;
  reg_exp_stat = sr;
  redraw;
}  // cdif_save

macro cdif_config
{
/******************************************************************************
															 Multi-Edit Macro
															 16-Feb-93  23:49

	Macro   : cdif_config
	Function: Displays a dialog box to allow changing the CDIFF colors.
  Syntax  : rm('cdif_config');
	Entry   : None.
	Exit    : CDIFF Colors set.

								 Copyright (C) 1993 by Small Systems Support.
***********************************************************************(ldh)**/
  int x1,
      y1,
      jx
      ;

  str  gstr[4], event_str[10] = '@EVCS#';

  if (@cdif_acolor == 0) {
    @cdif_acolor = 32;
  }
  if (@cdif_dcolor == 0) {
    @cdif_dcolor = 64;
  }

  put_box(4, 5, 31, 13 + 5, 0, m_b_color, 'CDIFF COLORS', true);
  set_global_str(Event_Str + '1', '/T=Done/KC=<ESC>/W=8/K1=27/K2=1/R=0/Y=17/X=12');
  rm('UserIn^CheckEvents /M=2/F=1/G=' + event_str + '/#=1');

  return_int = 1;
do_loop:
  x1 = 6;
  y1 = 10;
  jx = (w_b_color >> 4) + ((W_B_COLOR & 0xf7) << 4);
  Write('ÚÄÄÄÄCDIFF COLORSÄÄÄÄ·', x1, y1, 0, w_b_color );
  Write('³ Normal text        º', x1, y1 + 1, 0, w_b_color );
  Draw_Attr( x1 + 1, y1 + 1, w_t_color, 20 );
  Write('³ Added line text    º', x1, y1 + 2, 0, w_b_color );
  Draw_Attr( x1 + 1, y1 + 2, @cdif_acolor, 20 );
  Write('³ Deleted line text  º', x1, y1 + 3, 0, w_b_color );
  Draw_Attr( x1 + 1, y1 + 3, @cdif_dcolor, 20 );
  Write('ÔÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍ¼', x1, y1 + 4, 0, w_b_color );

  create_global_str(gstr + '1' ,  '/N=Added line text/H=INCO');
  create_global_str(gstr + '2' ,  '/N=Deleted line text');
  create_global_str(gstr + '3' ,  '/N=Save Colors');
  RM('USERIN^SUBMENU /#=3/B=1/A=1/GCLR=1/S=' + str(return_int) +
                  '/BC=' + str(box_count) +
                  '/X=5/Y=6/G=' + gstr);
  switch (return_int)
  {
    case 1 :
      jx = return_int;
      return_int = @cdif_acolor;
      rm('setup^colorchart /X=50');
      if (return_int != -1) {
        set_global_int('@cdif_acolor', return_int);
      }
      return_int = jx;
      break;

    case 2 :
      jx = return_int;
      return_int = @cdif_dcolor;
      rm('setup^colorchart /X=50');
      if (return_int != -1) {
        set_global_int('@cdif_dcolor', return_int);
      }
      return_int = jx;
      break;

		case 3:
			kill_box;
			rm('cdif_save');
			goto exit;
			break;

    case -2 :
      call check_event;
      if (return_int == -1)
      {
				kill_box;
        goto exit;
      }
      break;

    default:
			kill_box;
      goto exit;
  }
  goto do_loop;


check_event:
  return_int = 0;
  if (check_key) {
    if ((key1 == 0) && (key2 == 250)) {
      rm('UserIn^CheckEvents /M=1/G=' + event_str + '/#=1');
      if (return_int) {
        return_int = - 1;
      }
    }
  }
  ret;

exit:
}
