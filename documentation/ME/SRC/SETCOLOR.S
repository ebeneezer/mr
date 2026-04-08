macro SETCOLOR TRANS2 {
/*******************************************************************************
                               MULTI-EDIT MACRO

NAME:  SETCOLOR

DESCRIPTION:  This is the Color installation menu.

PARAMETERS:   /M=1   Do window color setup ONLY.  Return results in
                     RETURN_STR =
                        '/CTC=' + str( t_color ) +
                        '/CCC=' + str( c_color ) +
                        '/CBC=' + str( b_color ) +
                        '/CHC=' + str( h_color ) +
                        '/CSC=' + str( s_color ) +
                        '/CEC=' + str( eof_color );

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

  int X1,Y1,J1,jb,jx,J2,old_color;
  int  bc, ts,
           var_ofs,
           var_seg,
           t_w_t_color,
           t_w_h_color,
           t_w_c_color,
           t_w_eof_color,
           t_w_s_color,
           t_w_b_color,
           t_m_h_color,
           t_m_t_color,
           t_m_s_color,
           t_m_b_color,
           t_fkey_color,
           t_fnum_color,
           t_stat1_color,
           t_stat2_color,
           t_error_color,
           t_message_color,
           t_working_color,
           t_shadow_color,
           t_h_t_color,
           t_h_h_color,
           t_h_b_color,
           t_h_s_color,
           t_h_f_color,
           t_h_r_color,
           t_h_t1_color,
           t_h_t2_color,
           t_h_t3_color,
           t_cb_t_color,
           t_cb_h_color,
           t_cb_s_color,
           t_m_k_color,
           t_w_l_color,
           t_w_lb_color,
           t_button_shadow_c,

           t_background_color,
           t_button_color,
           t_button_key_color,

           t_reserve_word_color,
           t_symbol_color,
           t_eol_comment_color,
           t_string_char_color,
           t_comment1_color,
           t_comment2_color,
           t_numeric_color,
           choice,
           active_window,
           foreground,
           background,
           mono,
           menu = menu_create,
           menu2,

           color_mode = parse_int('/M=', mparm_str);

  str  gstr[4], bstr[10] = '', event_str[10] = '@EVCS#',
      Syntax_Str[2048] = "";
  char Shadow_Char1;
  refresh = false;
  X1 = 3;
  Y1 = 3;

/* Store all current colors into variables */

  call SET_TEMP_COLORS;

  bc = box_count;

  if(  color_mode == 1  ) {
    if(  xpos('/CTC=', return_str, 1)  ) {
      t_w_t_color = parse_int( '/CTC=', return_str);
      t_w_c_color = parse_int( '/CCC=', return_str);
      t_w_b_color = parse_int( '/CBC=', return_str);
      t_w_h_color = parse_int( '/CHC=', return_str);
      t_w_s_color = parse_int( '/CSC=', return_str);
      t_w_l_color = parse_int( '/CLC=', return_str);
      t_w_lb_color = parse_int( '/CLBC=', return_str);
      t_w_eof_color = parse_int( '/CEC=', return_str);
    }
    call do_window_colors;
    return_str = '/CTC=' + str(t_w_t_color ) +
                        '/CCC=' + str( t_w_c_color ) +
                        '/CBC=' + str( t_w_b_color ) +
                        '/CHC=' + str( t_w_h_color ) +
                        '/CSC=' + str( t_w_s_color ) +
                        '/CLC=' + str( t_w_l_color ) +
                        '/CLBC=' + str( t_w_lb_color ) +
                        '/CEC=' + str( t_w_eof_color );
    goto exitx;
  }

MAIN_MENU_BOX:
  put_box( 2, 4, 33, 12, 0, m_b_color, 'COLOR SETUP', true );
main_menu:
  y1 = 11;
  x1 = 12;
  call set_ev;
  create_global_str(gstr + '1' ,  '/N=Window colors/H=INCO');
  create_global_str(gstr + '2' ,  '/N=Menu//Dialog//Button colors');
  create_global_str(gstr + '3' ,  '/N=Help colors');
  create_global_str(gstr + '4' ,  '/N=Syntax highlighting colors');
  create_global_str(gstr + '5' ,  '/N=Other colors');
  create_global_str(gstr + '6' ,  '/N=color Templates');
  RM('USERIN^SUBMENU /#=6/A=1/GCLR=1/BC=' + str(bc) +
                  '/S=' + str(j1) +
                  '/X=2/Y=4/B=1/BO=1' +
                  '/G=' + gstr);
  j1 = return_int;
  switch ( Return_Int )
  {
    case 1 :
      call do_window_colors;
      break;

    case 2 :
      call do_menu_colors;
      break;

    case 3 :
      call do_help_colors;
      break;

    case 4 :
      call do_syntax_colors;
      break;

    case 5 :
      call do_other_colors;
      break;

    case 6 :
      RM("COLOR_TEMPLATES /NOWININIT=1");
      if (return_int) {
        Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
        call SET_TEMP_COLORS;
        kill_Box;
        goto MAIN_MENU_BOX;
      }
      break;

    case -2 :
      call check_event;
      if ( return_int == -1 )
        goto exit;
      break;

    default:
      goto exit;
  }
  check_key;
  goto main_menu;


do_window_colors:
  put_box( 4, 5, 31, 13 + 11, 0, m_t_color, 'WINDOW COLORS', true );
  y1 = 23;
  x1 = 12;
  call set_ev;

  return_int = 1;
do_window_loop:
  x1 = 6;
  y1 = 15;
  jx = (t_w_b_color >> 4) + ((T_W_B_COLOR & 0xf7) << 4);
  Write('É*¸FÕÍÍÍÍ?No-File?Í»', x1, y1, 0, t_w_b_color );
  Draw_Attr( x1 + 19, y1, jx, 2 );
  Draw_Attr( x1 + 1, y1, t_w_s_color, 1 );
  Draw_Attr( x1 + 3, y1, t_w_s_color, 1 );
  Draw_Attr( x1 + 9, y1, t_w_s_color, 9 );
  Write('ºNormal text         ', x1, y1 + 1, 0, t_w_b_color );
  Draw_Attr( x1 + 21, y1 + 1, jx, 1 );
  Draw_Attr( x1 + 1, y1 + 1, t_w_t_color, 20 );
  Write('ºChanged text        ²', x1, y1 + 2, 0, t_w_b_color );
  Draw_Attr( x1 + 1, y1 + 2, t_w_c_color, 20 );
  Write('ºHighlighted text    °', x1, y1 + 3, 0, t_w_b_color );
  Draw_Attr( x1 + 1, y1 + 3, t_w_h_color, 20 );
  Write('ºCurrent line        °', x1, y1 + 4, 0, t_w_b_color );
  Draw_Attr( x1 + 1, y1 + 4, t_w_l_color, 20 );
  Write('ºCurrent line in blck°', x1, y1 + 5, 0, t_w_b_color );
  Draw_Attr( x1 + 1, y1 + 5, t_w_lb_color, 20 );
  Write('º>> EOF <<           ', x1,y1 + 6, 0, t_w_b_color );
  Draw_Attr( x1 + 21, y1 + 6, jx, 1 );
  Draw_Attr( x1 + 1, y1 + 6, t_w_eof_color, 20 );
  Write('È²°°°°°°°°°°°°°°°°°|26¼', x1,y1 + 7, 0, t_w_b_color );
  Draw_Attr( x1 + 1, y1 + 7, jx, 1 );
  Draw_Attr( x1 + 20, y1 + 7, jx, 1 );

  create_global_str(gstr + '1' ,  '/N=Text/H=INCO');
  create_global_str(gstr + '2' ,  '/N=Changed-Text');
  create_global_str(gstr + '3' ,  '/N=Highlighted-Text');
  create_global_str(gstr + '4' ,  '/N=cUrrent line');
  create_global_str(gstr + '5' ,  '/N=cuRrent line in block');
  create_global_str(gstr + '6' ,  '/N=Window-border');
  create_global_str(gstr + '7' ,  '/N=window-Bold');
  create_global_str(gstr + '8' ,  '/N=End-Of-File');
  RM('USERIN^SUBMENU /#=8/B=1/A=1/GCLR=1/S=' + str(return_int) +
                  '/BC=' + str(box_count) +
                  '/X=5/Y=6/G=' + gstr);
  var_seg = 0;
  switch (Return_Int)
  {
    case 1 :
      var_seg = seg( t_w_t_color );
      var_ofs = ofs( t_w_t_color );
      break;

    case 2 :
      var_seg = seg( t_w_c_color );
      var_ofs = ofs( t_w_c_color );
      break;

    case 3 :
      var_seg = seg( t_w_h_color );
      var_ofs = ofs( t_w_h_color );
      break;

    case 6 :
      var_seg = seg( t_w_b_color );
      var_ofs = ofs( t_w_b_color );
      break;

    case 7 :
      var_seg = seg( t_w_s_color );
      var_ofs = ofs( t_w_s_color );
      break;

    case 4 :
      var_seg = seg( t_w_l_color );
      var_ofs = ofs( t_w_l_color );
      break;

    case 5 :
      var_seg = seg( t_w_lb_color );
      var_ofs = ofs( t_w_lb_color );
      break;

    case 8 :
      var_seg = seg( t_w_eof_color );
      var_ofs = ofs( t_w_eof_color );
      break;

    case -2 :
      call check_event;
      if ( return_int == -1 )
      {
        kill_box;
        ret;
      }
      break;

    default:
      kill_box;
      ret;
  }
  call color_chart;
  goto do_window_loop;


set_ev:
  Set_Global_Str(Event_Str + '1', '/T=Done/KC=<ESC>/W=8/K1=27/K2=1/R=0/Y=' + str( y1 ) + '/X=' + str( x1 ));
  RM('UserIn^CheckEvents /M=2/F=1/G=' + event_str + '/#=1');
  ret;

check_event:
  return_int = 0;
  if(  check_key  ) {
    if(  (key1 == 0) & (key2 == 250)  ) {
      RM('UserIn^CheckEvents /M=1/G=' + event_str + '/#=1');
      if(  return_int  ) {
        return_int = - 1;
      }
    }
  }
  ret;

do_menu_colors:
  put_box( 4,5, 32, 13 + 11, 0, m_t_color, 'MENU / DIALOG COLORS', true );
  y1 = 13 + 10;
  x1 = 13;
  call set_ev;
  return_int = 1;
do_menu_loop:
  x1 = 6;
  y1 = 17;
  Write( 'ÚÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄBOX',x1,y1,0,t_m_b_color);
  Write( '³',x1,y1+1,0,t_m_b_color);
  Write( '³',x1,y1+2,0,t_m_b_color);
  write(  '                       ',x1+1,y1+1,0,t_m_t_color);
  write(  '                       ',x1+1,y1+2,0,t_m_t_color);
  write(  ' () Select ',x1+1,y1+2,0,t_cb_h_color);
  write(  'S',x1+6,y1+2,0,t_cb_s_color);
  write(  ' ( ) Not-se',x1+13,y1+2,0,t_cb_t_color);
  write(  'N',x1+18,y1+2,0,t_cb_s_color);
  Write( '³                       ',x1,y1+3,0,t_m_b_color);
  Write( '³                       ',x1,y1+4,0,t_m_b_color);
  Write( 'ÔÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍ',x1,y1+5,0,t_m_b_color);
  Write( 'Button',x1+12,y1+3,0,t_button_color);
  Write( '<KEY>',x1+12+6,y1+3,0,t_button_key_color);
  draw_char( 223, x1 + 13, y1 + 4, t_button_shadow_c, 11 );
  draw_char( 220, x1 + 12 + 11, y1 + 3,t_button_shadow_c, 1 );
  Write( 'Window Menu      Block',x1+1,y1+1,0,t_m_t_color);
  Write( 'File',x1+13,y1+1,0,t_m_k_color);
  Write( 'Window',x1+1,y1+1,0,t_m_h_color);
  Write('M',x1+8,y1+1,0,t_m_s_color);
  Write('B',x1+18,y1+1,0,t_m_s_color);

  create_global_str(gstr + '1' , '/N=menu-Text/H=INCO');
  create_global_str(gstr + '2' , '/N=menu-Highlight');
  create_global_str(gstr + '3' , '/N=menu-Bold');
  create_global_str(gstr + '4' , '/N=menu-sKip');
  create_global_str(gstr + '5' , '/N=Menu-border');
  create_global_str(gstr + '6' , '/N=bUtton');
  create_global_str(gstr + '7' , '/N=button-Key');
  create_global_str(gstr + '8' , '/N=button-shAdow');
  create_global_str(gstr + '9' , '/N=Select');
  create_global_str(gstr + '10' , '/N=Not-select');
  create_global_str(gstr + '11' , '/N=Checkbox bold');
  RM('USERIN^SUBMENU /#=11/B=1/A=1/GCLR=1/S=' + str(return_int) +
                  '/BC=' + str(box_count) +
                  '/X=5/Y=6/G=' + gstr);

  var_seg = 0;
  switch ( Return_Int )
  {
    case  1 :
      var_seg = seg( t_m_t_color );
      var_ofs = ofs( t_m_t_color );
      break;

    case  2 :
      var_seg = seg( t_m_h_color );
      var_ofs = ofs( t_m_h_color );
      break;

    case  3 :
      var_seg = seg( t_m_s_color );
      var_ofs = ofs( t_m_s_color );
      break;

    case  4 :
      var_seg = seg( t_m_k_color );
      var_ofs = ofs( t_m_k_color );
      break;

    case  5 :
      var_seg = seg( t_m_b_color );
      var_ofs = ofs( t_m_b_color );
      break;

    case  6 :
      var_seg = seg( t_button_color );
      var_ofs = ofs( t_button_color );
      break;

    case  7 :
      var_seg = seg( t_button_key_color );
      var_ofs = ofs( t_button_key_color );
      break;

    case  8 :
      var_seg = seg( t_button_shadow_c );
      var_ofs = ofs( t_button_shadow_c );
      break;

    case  9 :
      var_ofs = ofs(t_cb_h_color);
      var_seg = seg(t_cb_h_color);
      break;

    case 10 :
      var_ofs = ofs(t_cb_t_color);
      var_seg = seg(t_cb_t_color);
      break;

    case 11 :
      var_ofs = ofs(t_cb_s_color);
      var_seg = seg(t_cb_s_color);
      break;

    case -2 :
      call check_event;
      if ( return_int == -1 )
      {
        kill_box;
        ret;
      }
      break;

    default:
      kill_box;
      ret;
  }
  call color_chart;
  goto do_menu_loop;


do_help_colors:
  put_box( 4,5, 32, 13 + 11, 0, m_t_color, 'HELP COLORS', true );
  y1 = 23;
  x1 = 13;
  call set_ev;

  return_int = 1;
do_help_loop:
  x1 = 6;
  y1 = 15;

  draw_outline( x1, y1 + 1, x1 + 21, y1 + 7, t_h_b_color);
  write('SAMPLE HELP', x1 + 5, y1 + 1, 0, t_h_b_color);
  Write('  CHAPTER HEADING   ', x1 + 1, y1 + 2, 0, t_h_s_color);
  Write('This is help text.  ', x1 + 1, y1 + 3, 0, t_h_t_color);
  Write('This is a           ', x1 + 1, y1 + 4, 0, t_h_t_color);
  Write('LINK', x1 + 11, y1 + 4, 0, t_h_r_color);
  Write('Highlighted         ', x1 + 1, y1 + 5, 0, t_h_t_color);
  Write('LINK', x1 + 13, y1 + 5, 0, t_h_h_color);
  Write('Attr1, ', x1 + 1, y1 + 6, 0, t_h_t1_color);
  Write('Attr2, ', x1 + 8, y1 + 6, 0, t_h_t2_color);
  Write('Attr3 ', x1 + 15, y1 + 6, 0, t_h_t3_color);
  Write('<F1>Index,<F6>Search,<', x1, y1 + 7, 0, t_h_f_color);

  create_global_str(gstr + '1' , '/N=help-Text/H=INCO');
  create_global_str(gstr + '2' , '/N=help-Highlight');
  create_global_str(gstr + '3' , '/N=help-Chapter');
  create_global_str(gstr + '4' , '/N=help-bOrder');
  create_global_str(gstr + '5' , '/N=help-Link');
  create_global_str(gstr + '6' , '/N=help-F-keys');
  create_global_str(gstr + '7' , '/N=help-attr-1');
  create_global_str(gstr + '8' , '/N=help-attr-2');
  create_global_str(gstr + '9' , '/N=help-attr-3');
  RM('USERIN^SUBMENU /#=9/B=1/A=1/GCLR=1/S=' + str(return_int) +
                  '/BC=' + str(box_count) +
                  '/X=5/Y=6/G=' + gstr);

  var_seg = 0;
  switch ( Return_Int )
  {
    case 1 :
      var_ofs = ofs(t_h_t_color);
      var_seg = seg(t_h_t_color);
      break;

    case 2 :
      var_ofs = ofs(t_h_h_color);
      var_seg = seg(t_h_h_color);
      break;

    case 3 :
      var_ofs = ofs(t_h_s_color);
      var_seg = seg(t_h_s_color);
      break;

    case 4 :
      var_ofs = ofs(t_h_b_color);
      var_seg = seg(t_h_b_color);
      break;

    case 5 :
      var_ofs = ofs(t_h_r_color);
      var_seg = seg(t_h_r_color);
      break;

    case 6 :
      var_ofs = ofs(t_h_f_color);
      var_seg = seg(t_h_f_color);
      break;

    case 7 :
      var_ofs = ofs(t_h_t1_color);
      var_seg = seg(t_h_t1_color);
      break;

    case 8 :
      var_ofs = ofs(t_h_t2_color);
      var_seg = seg(t_h_t2_color);
      break;

    case 9 :
      var_ofs = ofs(t_h_t3_color);
      var_seg = seg(t_h_t3_color);
      break;

    case -2 :
      call check_event;
      if ( return_int == -1 )
      {
        kill_box;
        ret;
      }
      break;

    default:
      kill_box;
      ret;
  }
  call color_chart;
  goto do_help_loop;


do_syntax_colors:
  menu2 = menu_create;
  intr($11);
  mono = (r_ax & $0030) == $0030;
  active_window = window_id;
  refresh = false;
  create_window;
  size_window(5,13,33,22);
  window_attr = 0x1E;
  c_color = t_w_t_color;
//  l_color = 0;
  put_line('Reserved words: if while');
  down;
  put_line('Symbols: <>=+-*/');
  down;
  put_line('"Strings" ''strings''');
  down;
  put_line('0xF3 12345 10.00');
  down;
  put_line('// End of line comments');
  down;
  put_line('/* Open/close comments 1 */');
  down;
  put_line('(* Open/close comments 2 *)');
  down;
  put_line('// Current line');
  file_changed = false;
  put_box( 4,3,38,24,0,m_t_color,'SYNTAX HIGHLIGHTING COLORS',true );
  y1 = 23;
  x1 = 14;
  call set_ev;
  draw_outline(5,13,33,22,w_b_color);
  Write('SYNTAX HIGHLIGHTING',10,14,0,w_b_color);

//  return_int = 1;
  choice = 1;
do_syntax_loop:

  set_keywords( "@TEMP_KEYS",   // name
                false,           // TRUE = case sensitive
                "IW",  // scan chars for reserved words
                "<>=+-/*()'""",  // scan chars for everything else
                //  reserved words 1
                " WHILE IF ",
                // reserved words 2
                "",
                "",
                "",
                "",
                t_reserve_word_color,  // reserved words attribute
                "<>=+-*/", t_symbol_color, // symbols and attribute
                "'""", t_string_char_color, "", // string chars, attribute and literal char
                '//',t_eol_comment_color,  // eol comment and attribute
                0,
                '',0,0,
                '/*', '*/', t_comment1_color,  // open/close comment 1 and attribute
                '(*', '*)', t_comment2_color, // open/close comment 2 and attribute
                '', t_numeric_color
                );
  keywords_str = "@TEMP_KEYS";
  refresh = true;
  redraw;
  refresh = false;

  menu_set_item(menu2,1,"Reserved words","","/H=INCOSYNTAX",0,0,0);
  menu_set_item(menu2,2,"Symbols","","",0,0,0);
  menu_set_item(menu2,3,"sTring constants","","",0,0,0);
  menu_set_item(menu2,4,"Numeric constants","","",0,0,0);
  menu_set_item(menu2,5,"End-of-line comments","","",0,0,0);
  menu_set_item(menu2,6,"Open//close comments 1","","",0,0,0);
  menu_set_item(menu2,7,"open//Close comments 2","","",0,0,0);
  menu_set_item(menu2,8,"syntax Highlighting enabled","","",0,Keyword_highlighting,0);
  menu_set_item(menu2,9,"current Line overrides syntax","","",0,Keyword_Cline_Override,0);
  return_int = menu2;
  RM('USERIN^SUBMENU /HN=1/#=9/B=1/A=1/GCLR=1/S=' + str(choice) +
                  '/BC=' + str(box_count) +
                  '/X=5/Y=4/G=' + gstr);
  var_seg = 0;
  choice = return_int;
  switch ( choice )
  {
    case 1 :
      var_seg = seg(t_reserve_word_color);
      var_ofs = ofs(t_reserve_word_color);
      Syntax_Str = "RESERVED WORDS";
      break;

    case 2 :
      var_seg = seg(t_symbol_color);
      var_ofs = ofs(t_symbol_color);
      Syntax_Str = "SYMBOLS";
      break;

    case 3 :
      var_seg = seg(t_string_char_color);
      var_ofs = ofs(t_string_char_color);
      Syntax_Str = "STRINGS";
      break;

    case 4 :
      var_seg = seg(t_numeric_color);
      var_ofs = ofs(t_numeric_color);
      Syntax_Str = "NUMERIC";
      break;

    case 5 :
      var_seg = seg(t_eol_comment_color);
      var_ofs = ofs(t_eol_comment_color);
      Syntax_Str = "END-OF-LINE COMMENTS";
      break;

    case 6 :
      var_seg = seg(t_comment1_color);
      var_ofs = ofs(t_comment1_color);
      Syntax_Str = "OPEN//CLOSE COMMENTS 1";
      break;

    case 7 :
      var_seg = seg(t_comment2_color);
      var_ofs = ofs(t_comment2_color);
      Syntax_Str = "OPEN//CLOSE COMMENTS 2";
      break;

    case 8 :
      Keyword_Highlighting = !Keyword_Highlighting;
/* This is a work-around for a bug in the menu routines that won't redraw a
 checkmark if unchecked
*/
      draw_char(32,5,11,m_h_color,1);
      Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
      goto do_syntax_loop;
      break;

    case 9 :
      Keyword_Cline_Override = !Keyword_Cline_Override;
/* This is a work-around for a bug in the menu routines that won't redraw a
 checkmark if unchecked
*/
      draw_char(32,5,12,m_h_color,1);
      Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
      goto do_syntax_loop;
      break;

    case -2 :
      call check_event;
      if ( return_int == -1 ) {
        kill_box;
        goto SYNTAX_EXIT;
      }
      goto do_syntax_loop;
      break;

    default:
      kill_box;
      goto SYNTAX_EXIT;
  }
  foreground = peek(var_seg,var_ofs) & 0xF;
  background = (peek(var_seg,var_ofs) >> 4) & 7;

  menu_set_item(menu,1,"Foreground","","/QK=1/L=1/C=5",10,1,0);
  menu_set_item(menu,2,"","","/L=/C=2/G=@T_SYNTAX_F/R=0",12, foreground < 1 ,0);
  menu_set_item(menu,3,"","","/L=/C=2/G=@T_SYNTAX_F/R=1",12, foreground == 1 ,0);
  menu_set_item(menu,4,"","","/L=/C=2/G=@T_SYNTAX_F/R=2",12, foreground == 2 ,0);
  menu_set_item(menu,5,"","","/L=/C=2/G=@T_SYNTAX_F/R=3",12, foreground == 3 ,0);
  menu_set_item(menu,6,"","","/L=/C=2/G=@T_SYNTAX_F/R=4",12, foreground == 4 ,0);
  menu_set_item(menu,7,"","","/L=/C=2/G=@T_SYNTAX_F/R=5",12, foreground == 5 ,0);
  menu_set_item(menu,8,"","","/L=/C=2/G=@T_SYNTAX_F/R=6",12, foreground == 6 ,0);
  menu_set_item(menu,9,"","","/L=/C=2/G=@T_SYNTAX_F/R=7",12, foreground == 7 ,0);
  menu_set_item(menu,10,"","","/L=2/C=12/G=@T_SYNTAX_F/R=8",12, foreground == 8 ,0);
  menu_set_item(menu,11,"","","/L=3/C=12/G=@T_SYNTAX_F/R=9",12, foreground == 9 ,0);
  menu_set_item(menu,12,"","","/L=4/C=12/G=@T_SYNTAX_F/R=10",12, foreground == 10 ,0);
  menu_set_item(menu,13,"","","/L=5/C=12/G=@T_SYNTAX_F/R=11",12, foreground == 11 ,0);
  menu_set_item(menu,14,"","","/L=6/C=12/G=@T_SYNTAX_F/R=12",12, foreground == 12 ,0);
  menu_set_item(menu,15,"","","/L=7/C=12/G=@T_SYNTAX_F/R=13",12, foreground == 13 ,0);
  menu_set_item(menu,16,"","","/L=8/C=12/G=@T_SYNTAX_F/R=14",12, foreground == 14 ,0);
  menu_set_item(menu,17,"","","/L=9/C=12/G=@T_SYNTAX_F/R=15",12, foreground == 15 ,0);

  menu_set_item(menu,18,"ÛÛ","","/L=2/C=8/ATTR=" + str(mono * 8),10,1,0);
  menu_set_item(menu,19,copy("ÛÛ  ",(mono * 2) + 1,2),"","/L=3/C=8/ATTR=1",10,1,0);
  menu_set_item(menu,20,"ÛÛ","","/L=4/C=8/ATTR=2",10,1,0);
  menu_set_item(menu,21,"ÛÛ","","/L=5/C=8/ATTR=3",10,1,0);
  menu_set_item(menu,22,"ÛÛ","","/L=6/C=8/ATTR=4",10,1,0);
  menu_set_item(menu,23,"ÛÛ","","/L=7/C=8/ATTR=5",10,1,0);
  menu_set_item(menu,24,"ÛÛ","","/L=8/C=8/ATTR=6",10,1,0);
  menu_set_item(menu,25,"ÛÛ","","/L=9/C=8/ATTR=7",10,1,0);
  menu_set_item(menu,26,"ÛÛ","","/L=2/C=18/ATTR=8",10,1,0);
//  menu_set_item(menu,27,"ÛÛ","","/L=3/C=18/ATTR=9",10,1,0);
  menu_set_item(menu,27,copy("ÛÛ  ",(mono * 2) + 1,2),"","/L=3/C=18/ATTR=9",10,1,0);
  menu_set_item(menu,28,"ÛÛ","","/L=4/C=18/ATTR=10",10,1,0);
  menu_set_item(menu,29,"ÛÛ","","/L=5/C=18/ATTR=11",10,1,0);
  menu_set_item(menu,30,"ÛÛ","","/L=6/C=18/ATTR=12",10,1,0);
  menu_set_item(menu,31,"ÛÛ","","/L=7/C=18/ATTR=13",10,1,0);
  menu_set_item(menu,32,"ÛÛ","","/L=8/C=18/ATTR=14",10,1,0);
  menu_set_item(menu,33,"ÛÛ","","/L=9/C=18/ATTR=15",10,1,0);
  menu_set_item(menu,34,"active","","/L=11/C=5/QK=1",13,(peek(var_seg,var_ofs + 1) & 1),0);


  menu_set_item(menu,35,"Background  ","","/L=1/C=25/QK=1",10,1,0);
  menu_set_item(menu,36,"","","/L=2/C=25/G=@T_SYNTAX_B/R=0",12, background < 1 ,0);
  menu_set_item(menu,37,"","","/L=3/C=25/G=@T_SYNTAX_B/R=1",12, background == 1 ,0);
  menu_set_item(menu,38,"","","/L=4/C=25/G=@T_SYNTAX_B/R=2",12, background == 2 ,0);
  menu_set_item(menu,39,"","","/L=5/C=25/G=@T_SYNTAX_B/R=3",12, background == 3 ,0);
  menu_set_item(menu,40,"","","/L=6/C=25/G=@T_SYNTAX_B/R=4",12, background == 4 ,0);
  menu_set_item(menu,41,"","","/L=7/C=25/G=@T_SYNTAX_B/R=5",12, background == 5 ,0);
  menu_set_item(menu,42,"","","/L=8/C=25/G=@T_SYNTAX_B/R=6",12, background == 6 ,0);
  menu_set_item(menu,43,"","","/L=9/C=25/G=@T_SYNTAX_B/R=7",12, background == 7 ,0);
  menu_set_item(menu,44,"ÛÛ","","/L=2/C=31/ATTR=" + str(mono * 8),10,1,0);
  menu_set_item(menu,45,"ÛÛ","","/L=3/C=31/ATTR=" + copy("18",mono + 1,1),10,1,0);
  menu_set_item(menu,46,"ÛÛ","","/L=4/C=31/ATTR=" + copy("28",mono + 1,1),10,1,0);
  menu_set_item(menu,47,"ÛÛ","","/L=5/C=31/ATTR=" + copy("38",mono + 1,1),10,1,0);
  menu_set_item(menu,48,"ÛÛ","","/L=6/C=31/ATTR=" + copy("48",mono + 1,1),10,1,0);
  menu_set_item(menu,49,"ÛÛ","","/L=7/C=31/ATTR=" + copy("58",mono + 1,1),10,1,0);
  menu_set_item(menu,50,"ÛÛ","","/L=8/C=31/ATTR=" + copy("68",mono + 1,1),10,1,0);
  menu_set_item(menu,51,"ÛÛ","","/L=9/C=31/ATTR=7",10,1,0);
  menu_set_item(menu,52,"active","","/L=11/C=25/QK=2",13,(peek(var_seg,var_ofs + 1) & 2) >> 1,0);
  menu_set_item(menu,53,"Blink ","","/L=12/C=25/QK=2",13,peek(var_seg,var_ofs) >> 7,0);
  return_int = menu;
  RM("USERIN^DATA_IN /HN=1/POSG=!SYNTAX_POS/PRE=D/#=53/H=INCOSYNTAX%COLOR CHART/T=" + Syntax_Str +
      "/X=" + parse_str("/X=",global_str("!SYNTAX_POS")) +
      "/Y=" + parse_str("/Y=",global_str("!SYNTAX_POS"))
      );
  if (return_int == 1) {
    Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
    poke(var_seg,var_ofs,
        (global_int("@T_SYNTAX_B") << 4) | global_int("@T_SYNTAX_F") |
        (menu_item_int(menu,53,2) * 128)
        );
    poke(var_seg,var_ofs + 1,
        (menu_item_int(menu,52,2) << 1) | menu_item_int(menu,34,2));
  }

  return_int = choice;
  goto do_syntax_loop;

SYNTAX_EXIT:
  delete_window;
  menu_delete(menu2);
  switch_win_id(active_window);
  set_global_str("!SYNTAX_POS","");
  set_global_int("@T_SYNTAX_B",0);
  set_global_int("@T_SYNTAX_F",0);
  RET;


do_other_colors:
  put_box( 4,5, 35, 13 + 11, 0, m_t_color, 'OTHER COLORS', true );
  y1 = 23;
  x1 = 13;
  call set_ev;

  return_int = 1;
do_other_loop:
  x1 = 4;
  y1 = 17;

  Write( 'Message  WORKING  L:333 C:10', x1 + 1, y1, 0, t_stat1_color );
  draw_attr( x1 + 9, y1, t_working_color, 9 );
  draw_attr( x1 + 1, y1, t_message_color, 8 );
  draw_attr( x1 + 21, y1, t_stat2_color, 3 );
  draw_attr( x1 + 27, y1, t_stat2_color, 2 );
  Write('±±±±±±±±±±±±±±±±±±±±±±±±±±±±', x1 + 1, y1 + 1, 0, t_background_color );
  Write('±±±±±±±±±±±±±±±±±±±±±±±±±±±±', x1 + 1, y1 + 2, 0, t_background_color );
  Write('±±±±±±±±±±±±±±±±±±±±±±±±±±±±', x1 + 1, y1 + 3, 0, t_background_color );
  Write('±±±±±±±±±±±±±±±±±±±±±±±±±±±±', x1 + 1, y1 + 4, 0, t_background_color );
  put_box( x1 + 4, y1 + 2, x1 + 25, y1 + 4, 0, t_error_color, 'ERROR BOX', true );
  pop_box;
  Write('1Help 2Save 3Load 4Indent 5M', x1 + 1, y1 + 5, 0, t_fkey_color );
  draw_attr( x1 + 1, y1 + 5, t_fnum_color, 1 );
  draw_attr( x1 + 7, y1 + 5, t_fnum_color, 1 );
  draw_attr( x1 + 13, y1 + 5, t_fnum_color, 1 );
  draw_attr( x1 + 19, y1 + 5, t_fnum_color, 1 );
  draw_attr( x1 + 27, y1 + 5, t_fnum_color, 1 );


  create_global_str(gstr + '1' , '/N=Statusline/H=INCO');
  create_global_str(gstr + '2' , '/N=statusline-Bold');
  create_global_str(gstr + '3' , '/N=Fkey-Labels');
  create_global_str(gstr + '4' , '/N=fkey-Numbers');
  create_global_str(gstr + '5' , '/N=Error');
  create_global_str(gstr + '6' , '/N=Message');
  create_global_str(gstr + '7' , '/N=Working');
  create_global_str(gstr + '8' , '/N=sHadow');
  create_global_str(gstr + '9' , '/N=shadow-Character');
  create_global_str(gstr + '10' ,  '/N=bAckground color');

  RM('USERIN^SUBMENU /#=10/B=1/A=1/GCLR=1/S=' + str(return_int) +
                  '/BC=' + str(box_count) +
                  '/X=5/Y=6/G=' + gstr);

  var_seg = 0;
  switch ( Return_Int )
  {
    case 1 :
      var_ofs = ofs(t_stat1_color);
      var_seg = seg(t_stat1_color);
      break;

    case 2 :
      var_ofs = ofs(t_stat2_color);
      var_seg = seg(t_stat2_color);
      break;

    case 3 :
      var_ofs = ofs(t_fkey_color);
      var_seg = seg(t_fkey_color);
      break;

    case 4 :
      var_ofs = ofs(t_fnum_color);
      var_seg = seg(t_fnum_color);
      break;

    case 5 :
      var_ofs = ofs(t_error_color);
      var_seg = seg(t_error_color);
      break;

    case 6 :
      var_ofs = ofs(t_message_color);
      var_seg = seg(t_message_color);
      break;

    case 7 :
      var_ofs = ofs(t_working_color);
      var_seg = seg(t_working_color);
      break;

    case 8 :
      var_ofs = ofs(t_shadow_color);
      var_seg = seg(t_shadow_color);
      break;

    case 9 :
      jb = return_int;
      if( shadow_char1 == '|0')
        jb = 5;
      else
      {
        jb = ascii(shadow_char1) - 175;

//        MAKE_MESSAGE(STR(JB));
        if ( jb == 44 )
            jb = 4;
        if (jb < 1)
            jb = 1;
        if (jb > 4)
          jb = 1;
      }
      j2 = return_int;
      put_box(11, 13, 41, 21, 0, m_b_color, 'SELECT SHADOW CHARACTER',true);
      write('      |176|176|176|176', 12, 15, 0, m_s_color );
      write('      |177|177|177|177 ', 12, 16, 0, m_s_color );
      write('      |178|178|178|178',12, 17, 0, m_s_color );
      write('      |219|219|219|219', 12, 18, 0, m_s_color );
      write('      Transparent', 12, 19, 0, m_s_color );
      RM('userin^xmenu /T=1/X=' + str(12) + '/Y=' + str(15) + '/S=' + str(jb)
                  + '/M=1-->(INCO)2-->()3-->()4-->()5-->()');
      kill_box;
      jb = return_int;
      return_int = j2;
      if ( jb > 0 ) {
        Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
        shadow_char1 = char( jb + 175 );
        if(  jb == 4  )
          shadow_char1 =  '|219';
        if(  jb == 5  )
          shadow_char1 =  '|0';
      }
      shadow_char = shadow_char1;
      goto do_other_loop;
      break;

    case 10 :
      var_ofs = ofs(t_background_color);
      var_seg = seg(t_background_color);
      break;

    case -2 :
      call check_event;
      if ( return_int == -1 )
      {
        kill_box;
        ret;
      }
      break;

    default:
      kill_box;
      ret;
  }
  call color_chart;
  goto do_other_loop;

SET_TEMP_COLORS:
/* Store all current colors into variables */
  t_h_t_color = h_t_color & $f7;   /*  Limit foreground color of help-text to non-bright colors  */
  t_h_t1_color = h_t1_color;
  t_h_t2_color = h_t2_color;
  t_h_t3_color = h_t3_color;
  t_h_h_color = h_h_color;
  t_h_b_color = h_b_color;
  t_h_s_color = h_s_color;
  t_h_r_color = h_r_color;
  t_h_f_color = h_f_color;

  t_m_t_color = m_t_color;
  t_m_h_color = m_h_color;
  t_m_s_color = m_s_color;
  t_m_b_color = m_b_color;

  t_error_color   = error_color;
  t_message_color = message_color;
  t_working_color = working_color;
  t_shadow_color = shadow_color;


  t_w_t_color = w_t_color;
  t_w_h_color = w_h_color;
  t_w_c_color = w_c_color;
  t_w_eof_color = w_eof_color;
  t_w_s_color = w_s_color;
  t_w_b_color = w_b_color;
  t_fkey_color = fkey_color;
  t_fnum_color = fnum_color;
  t_stat1_color = stat1_color;
  t_stat2_color = stat2_color;
  t_background_color = background_color;
  J1 = 1;
  Shadow_Char1 = Shadow_Char;
  t_cb_t_color = cb_t_color;
  t_cb_h_color = cb_h_color;
  t_cb_s_color = cb_s_color;
  t_m_k_color = m_k_color;
  t_w_l_color = w_l_color;
  t_w_lb_color = w_lb_color;
  t_button_shadow_c = button_shadow_color;

  t_button_color = button_color;
  t_button_key_color = button_key_color;

  t_reserve_word_color = parse_int("/RWC=",Global_Str("@SYNTAX_COLORS"));
  t_symbol_color = parse_int("/SYC=",Global_Str("@SYNTAX_COLORS"));
  t_eol_comment_color = parse_int("/ECC=",Global_Str("@SYNTAX_COLORS"));
  t_string_char_color = parse_int("/SCC=",Global_Str("@SYNTAX_COLORS"));
  t_comment1_color = parse_int("/C1C=",Global_Str("@SYNTAX_COLORS"));
  t_comment2_color = parse_int("/C2C=",Global_Str("@SYNTAX_COLORS"));
  t_numeric_color = parse_int("/NCC=", global_str("@SYNTAX_COLORS"));
  RET;

color_chart:
  if(  var_seg != 0  ) {
    jx = return_int;
    return_int = peek( var_seg, var_ofs );
    rm('COLORCHART /X=50');
    if(  return_int != -1  ) {
      if( color_mode == 0  ) {
        Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $01);
      }

      if ( (var_ofs == ofs(t_shadow_color)) &&
           (var_seg == seg(t_shadow_color)) )
        Shadow_Color = Return_Int;

      poke( var_seg, var_ofs,  return_int );
    }
    return_int = jx;
  }
  ret;

exit:
/* Now, change the current colors to the new colors and get out */
  kill_box;


  m_t_color = t_m_t_color;
  m_h_color = t_m_h_color;
  m_b_color = t_m_b_color;
  m_s_color = t_m_s_color;
  m_k_color = t_m_k_color;
  cb_t_color = t_cb_t_color;
  cb_s_color = t_cb_s_color;
  cb_h_color = t_cb_h_color;
  button_color = t_button_color;
  button_key_color = t_button_key_color;
  button_shadow_color = t_button_shadow_c;

  h_t_color = t_h_t_color;
  h_h_color = t_h_h_color;
  h_b_color = t_h_b_color;
  h_s_color = t_h_s_color;
  h_r_color = t_h_r_color;
  h_f_color = t_h_f_color;
  h_t1_color = t_h_t1_color;
  h_t2_color = t_h_t2_color;
  h_t3_color = t_h_t3_color;

  error_color   = t_error_color;
  message_color = t_message_color;
  working_color = t_working_color;
  shadow_color = t_shadow_color;
  shadow_char = shadow_char1;

  w_t_color = t_w_t_color;
  w_h_color = t_w_h_color;
  w_c_color = t_w_c_color;
  w_eof_color = t_w_eof_color;
  w_s_color = t_w_s_color;
  w_b_color = t_w_b_color;
  w_l_color = t_w_l_color;
  w_lb_color = t_w_lb_color;

  d_t_color = m_t_color;
  d_h_color = m_h_color;
  d_s_color = m_s_color;
  d_b_color = m_b_color;

  fkey_color = t_fkey_color;
  fnum_color = t_fnum_color;
  stat1_color = t_stat1_color;
  stat2_color = t_stat2_color;
  background_color = t_background_color;

  Set_Global_Str("@SYNTAX_COLORS",
    "/RWC=" + Str(t_reserve_word_color) +
    "/SYC=" + Str(t_symbol_color) +
    "/ECC=" + Str(t_eol_comment_color) +
    "/SCC=" + Str(t_string_char_color) +
    "/C1C=" + Str(t_comment1_color) +
    "/C2C=" + Str(t_comment2_color) +
    "/NCC=" + Str(t_numeric_color)

  );

  j1 = 0;
  j2 = cur_window;
  Syntax_str = "";
  while(  j1 < window_count  ) {
    ++j1;
    switch_window(j1);
    if(  window_color_stat == 0  ) {
      t_color = w_t_color;
      h_color = w_h_color;
      eof_color = w_eof_color;
      s_color = w_s_color;
      b_color = w_b_color;
      c_color = w_c_color;
      l_color = w_l_color;
      lb_color = w_lb_color;
    }
    if (!xpos("" + get_extension(file_name) + "",syntax_str,1)) {
/* since resetting the syntax colors is quite time consuming, only do it once
  per extension */
      RM("SET_SYNTAX_PARMS /REINIT=1/RESET=1");
      Syntax_str = Syntax_str + get_extension(file_name) + "";
    }
  }
  switch_window( j2 );

exitx:
	if( parse_int("/RS=", mparm_str))
	{
		set_global_int("_reset_screen", 2 );
 		push_key( 27, 1 );
	}
  menu_delete( menu );
  return_int = 0;
  set_global_str( event_str + '1', '');
}

macro colorchart TRANS2 {
/*******************************************************************************
                                MULTI-EDIT MACRO

Name:  COLORCHART

Description:  Popup color chart for selecting screen attributes.

Parameters: /T= - Override the box title.
            RETURN_INT = the initial attribute.

Returns:    RETURN_INT = the selected attribute OR
            RETURN_INT = -1 if Cancel was selected.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  int jx,jy,jl, cc, move = true, bc = box_count,
          width = 45, height = 21, ll, x, y, cur_color, blink, ev_count;
  str  tstr[20], event_str[20], Box_Title;
  int  field_num, blink, old_color;

  if ( '' == (Box_Title=Parse_Str('/T=',MParm_Str)) )
    Box_Title='COLOR CHART';

  cur_color = return_int & $FF;
  blink = (cur_color > 127);
  cur_color = cur_color & $7F;
  jx = parse_int('/A=', mparm_str);
  field_num = jx & $7F;
  blink = ((jx & $80) != 0);
  x = parse_int('/X=', mparm_str);
  if(  x == 0  ) {
    x = 8;
  }
  if(  (x + width + 1) > screen_width  ) {
    x = screen_width - width - 1;
  }
  y = 3;

  Ev_Count = 3;
  event_str =  '@EV' + Str(Global_Int( 'MENU_LEVEL' )) + '#';

  call draw_chart;

loop:
  call reset_cur_color;
  call write_cur_color;
  read_key;
  if(  key1 == 27  ) {
    return_int = -1;
    goto exit;
  } else if(  key1 == 13  ) {
    return_int = cur_color;
    goto exit;
  } else if(  key1 == 9  ) {
    call toggle_blink;
  } else if(  key1 == 0  ) {
    if(  key2 == 77  ) {
      cur_color = cur_color + 16;
    } else if(  key2 == 75  ) {
      cur_color = cur_color - 16;
    } else if(  key2 == 80  ) {
      ++cur_color;
    } else if(  key2 == 72  ) {
      --cur_color;
    } else if(  key2 == 250  ) {
      RM('UserIn^CheckEvents /M=1/G=' + event_str + '/#=' + str(ev_count));
      if(  RETURN_INT != 0  ) {
        Return_Int = Parse_Int('/R=', return_str);
        if(  return_int == 0  ) {
          return_int = -1;
          goto exit;
        } else if(  return_int == 1  ) {
          return_int = cur_color;
          goto exit;
        } else if(  return_int == 2  ) {
          call toggle_blink;
        }
      } else if(  (mou_last_y == y) & (mou_last_x >= x) & (mou_last_x <= (x + width))  ) {
        return_int = 0;
        call move_box;
      } else if(  (mou_last_y > y) & (mou_last_y < (y + (height - 4))) &
        (mou_last_x > (x + 1)) & (mou_last_x < (x + width - 4))  ) {
        cur_color = (((mou_last_x - (x + 2)) / 5) * 16) + (mou_last_y - y - 1 );
        call reset_cur_color;
        call write_cur_color;
        return_int = cur_color;
        goto exit;
      }
    }
  }

  goto loop;

reset_cur_color:
  if(  cur_color > 127  ) {
    cur_color = (cur_color % 16) + 1;
    if(  cur_color > 15  ) {
      cur_color = 0;
    }
  } else if(  cur_color < 0  ) {
    cur_color = 127 - ( (0 - cur_color) % 16);
  }
/*  while box_count > (bc + 1) do
    kill_box;
  end;  */
  write( ' ', cc, ll, 0, old_color );
  write( ' ', cc + 4, ll, 0, old_color );
  ret;

write_cur_color:
  cc = ((cur_color / 16) * 5) + x + 2;
  ll = (cur_color % 16) + y + 1;
 /* put_box( cc - 1, ll - 1, cc + 5, ll + 1, 0, (cur_color AND $70) + $F , '', false );
  write( str(cur_color), cc + 1, ll, 0, cur_color ); */
  write( '|16', cc, ll, 0, (cur_color & $70) + $0F );
  write( '|17', cc + 4, ll, 0, (cur_color & $70) + $0F );
  gotoxy( cc + 1, ll );
  old_color = cur_color;
  ret;

move_box:
  call reset_cur_color;
  RM('WINDOW^MOVE_WIN /X1=' + str(x)+'/Y1=' + Str(y) + '/X2=' +
    Str(x + width - 2) + '/Y2=' + Str(y + height - 1) + '/MS=' + str(0) +
    '/MX1=1/MX2=' + Str(Screen_Width) +
    '/MY1=2/MY2=' + str(screen_length - 1) +
    '/MM=0/M=2/K=' + str(return_int));
  if(  (virtual_display == 0)  ) {
    kill_box;
  }
  x = parse_int('/X1=', return_str);
  y = parse_int('/Y1=', return_str);
  move = TRUE;
  override_screen_seg;
  call draw_chart;
  reset_screen_seg;
  update_virtual_display;
  reset_virtual_display;
  ret;


draw_chart:
  while(  box_count > (bc + 1)  ) {
    kill_box;
  }
  set_virtual_display;
  if(  move  ) {
    Set_Global_Str(Event_Str + '1', '/3D=1/T=Select/KC=<ENTER>/W=10/K1=13/K2=28/R=0/Y=' + str( y + 18) + '/X=' + str( x+ 16 ));
    Set_Global_Str(Event_Str + '2', '/3D=1/T=Cancel/KC=<ESC>/W=11/K1=27/K2=1/R=1/Y=' + str( y + 18) + '/X=' + str( x+ 31 ));
    call set_blink_button;
    put_box( x, y, x + width, y + height, 0, m_b_color, Box_Title, true );
    move = false;
  }
  jx = 0;
  jl = (blink * 128);
  cc = x + 2;
  ll = y + 1;
  while(  jx < 8  ) {
    jy = 0;
    while(  jy < 16  ) {
      tstr = str( jl );
      pad_str(tstr,4, ' ' );
      write( ' ' + tstr, cc, ll + jy, 0,jl );
      ++jy;
      ++jl;
    }
    ++jx;
    cc = cc + 5;
  }
  RM('UserIn^CheckEvents /M=2/F=1/G=' + event_str + '/#=' + str(ev_count));
  update_virtual_display;
  reset_virtual_display;
  call write_cur_color;
  ret;

toggle_blink:
  blink = (blink == 0);
  call set_blink_button;
  write('              ', x + 2, y + 18, 0, m_t_color );
  write('              ', x + 2, y + 19, 0, m_t_color );
  call draw_chart;
  ret;

set_blink_button:
  if(  blink  ) {
    Set_Global_Str(Event_Str + '3', '/3D=1/T=No blink/W=13/KC=<TAB>/K1=19/K2=15/R=2/Y=' + str( y + 18) + '/X=' + str( x+ 1 ));
  } else {
    Set_Global_Str(Event_Str + '3', '/3D=1/T=Blink/KC=<TAB>/W=10/K1=19/K2=15/R=2/Y=' + str( y + 18) + '/X=' + str( x+ 1 ));
  }
  ret;

exit:
  RM('UserIn^CheckEvents /M=3/G=' + event_str + '/#=' + str(ev_count));
  while(  box_count > bc  ) {
    kill_box;
  }

  Return_Int |= Blink << 7;
}

macro CUSTOMCOLOR TRANS2 {
/*******************************************************************************
                                MULTI-EDIT MACRO

Name:  CUSTOMCOLOR

Description: Creates an interface to configure extension specific colors.  To
             be run by EXTENS only!

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  str  tstr = return_str ;
  return_int = (xpos('/CTC=', tstr,1) != 0) + 1;
  RM('XMENU /B=1/T=1/L=WINDOW COLORS/M=Default()Custom()');
  if(  return_int == 2  ) {
    return_str = tstr;
    RM('SETCOLOR /M=1');
    return_str = 'Custom      ' + return_str;
  } else if(  return_int == 1  ) {
    return_str = 'Default';
  } else {
    return_str = tstr;
  }
  return_int = 1;
} /* CUSTOMCOLOR */

macro COLOR_TEMPLATES trans {
  str color_str[2048];
  int T_Int, t_refresh,
      t_insert_mode,
      t_reg_exp_stat;

  create_global_str("!COLOR_BUTTON1","/W=11/C=18/L=1/KC=<INS>/K1=0/K2=82/TP=11/H=/T=Create/R=10");

LIST:
  RM("DB /H=INCOTEMPLATES/F=MECONFIG/LT=COLOR TEMPLATES/DPT=COLORS.DB/LO=1/NE=1/NI=1/NC=1/NDF=1/ABT=Load/EBC=1/EBN=!COLOR_BUTTON/GLO=!COLOR_TEMPLATE");
  if (return_int == 1) {
// Load color template from DB file
    t_refresh = refresh;
    color_str = global_str("!COLOR_TEMPLATE");
    if (svl(color_str)) {
      working;
      w_t_color           = parse_int("WTC=",color_str);
      w_h_color           = parse_int("WHC=",color_str);
      w_c_color           = parse_int("WCC=",color_str);
      w_eof_color         = parse_int("WEC=",color_str);
      w_s_color           = parse_int("WSC=",color_str);
      w_b_color           = parse_int("WBC=",color_str);
      m_h_color           = parse_int("MHC=",color_str);
      m_t_color           = parse_int("MTC=",color_str);
      m_s_color           = parse_int("MSC=",color_str);
      m_b_color           = parse_int("MBC=",color_str);
      d_b_color           = parse_int("DBC=",color_str);
      d_h_color           = parse_int("DHC=",color_str);
      d_s_color           = parse_int("DSC=",color_str);
      d_t_color           = parse_int("DTC=",color_str);
      fkey_color          = parse_int("FKC=",color_str);
      fnum_color          = parse_int("FNC=",color_str);
      stat1_color         = parse_int("S1C=",color_str);
      stat2_color         = parse_int("S2C=",color_str);
      error_color         = parse_int("ERC=",color_str);
      message_color       = parse_int("MEC=",color_str);
      working_color       = parse_int("WKC=",color_str);
      shadow_color        = parse_int("SHC=",color_str);
      shadow_char         = char(parse_int("SHCH=",color_str));
      h_t_color           = parse_int("HTC=",color_str);
      h_h_color           = parse_int("HHC=",color_str);
      h_b_color           = parse_int("HBC=",color_str);
      h_s_color           = parse_int("HSC=",color_str);
      h_f_color           = parse_int("HFC=",color_str);
      h_r_color           = parse_int("HRC=",color_str);
      h_t1_color          = parse_int("H1C=",color_str);
      h_t2_color          = parse_int("H2C=",color_str);
      h_t3_color          = parse_int("H3C=",color_str);
      cb_t_color          = parse_int("CTC=",color_str);
      cb_h_color          = parse_int("CHC=",color_str);
      cb_s_color          = parse_int("CSC=",color_str);
      m_k_color           = parse_int("MKC=",color_str);
      w_l_color           = parse_int("WLC=",color_str);
      w_lb_color          = parse_int("WLB=",color_str);
      button_shadow_color = parse_int("BSC=",color_str);
      background_color    = parse_int("BGC=",color_str);
      button_color        = parse_int("BTC=",color_str);
      button_key_color    = parse_int("BKC=",color_str);

      /* SYNTAX HIGHLIGHTING COLORS */
      set_global_str("@SYNTAX_COLORS",
        "/RWC=" + parse_str("RWC=",color_str) +
        "/SYC=" + parse_str("SYC=",color_str) +
        "/ECC=" + parse_str("ECC=",color_str) +
        "/SCC=" + parse_str("SCC=",color_str) +
        "/C1C=" + parse_str("C1C=",color_str) +
        "/C2C=" + parse_str("C2C=",color_str) +
        "/NCC=" + parse_str("NCC=",color_str)
      );

      if (!parse_int("/NOWININIT=",mparm_str)) {
// reinit colors for all windows
        color_str = "";
        refresh = false;
        for (t_int = 0;t_int < window_count;++t_int) {
          switch_window(cur_window + 1);
          if(  window_color_stat == 0  ) {
            t_color = w_t_color;
            h_color = w_h_color;
            eof_color = w_eof_color;
            s_color = w_s_color;
            b_color = w_b_color;
            c_color = w_c_color;
            l_color = w_l_color;
            lb_color = w_lb_color;
          }
          if (!xpos("" + get_extension(file_name) + "",color_str,1)) {
    /* since resetting the syntax colors is quite time consuming, only do it once
      per extension */
            RM("SET_SYNTAX_PARMS /REINIT=1");
            color_str = color_str + get_extension(file_name) + "";
          }
        }
      }
      refresh = t_refresh;
      new_screen;
    }
  } else if (return_int == 10) {
// Save current colors as a new template
    t_insert_mode = insert_mode;
    t_reg_exp_stat = reg_exp_stat;
    t_int = window_id;

    return_str = "";
    RM("QUERYBOX /W=20/T=CREATE COLOR TEMPLATE/P=Enter a name:");
    if (return_int) {
      color_str = return_str;
      RM("SETCONFIG /DB=MECONFIG/T=COLORS.DB");
      if (return_int) {
        reg_exp_stat = true;
        eol;
        if (search_fwd("%{@*@*@*@*START@*@*@*@*$}|\f",0)) {
          if (cur_char != "\f") {
            Eol;
            insert_mode = true;
            cr;
            put_line(
              "NAME=" + color_str +
              "WTC=" + str(w_t_color) +
              "WHC=" + str(w_h_color) +
              "WCC=" + str(w_c_color) +
              "WEC=" + str(w_eof_color) +
              "WSC=" + str(w_s_color) +
              "WBC=" + str(w_b_color) +
              "MHC=" + str(m_h_color) +
              "MTC=" + str(m_t_color) +
              "MSC=" + str(m_s_color) +
              "MBC=" + str(m_b_color) +
              "DBC=" + str(d_b_color) +
              "DHC=" + str(d_h_color) +
              "DSC=" + str(d_s_color) +
              "DTC=" + str(d_t_color) +
              "FKC=" + str(fkey_color) +
              "FNC=" + str(fnum_color) +
              "S1C=" + str(stat1_color) +
              "S2C=" + str(stat2_color) +
              "ERC=" + str(error_color) +
              "MEC=" + str(message_color) +
              "WKC=" + str(working_color) +
              "SHC=" + str(shadow_color) +
              "SHCH=" + str(ascii(shadow_char)) +
              "HTC=" + str(h_t_color) +
              "HHC=" + str(h_h_color) +
              "HBC=" + str(h_b_color) +
              "HSC=" + str(h_s_color) +
              "HFC=" + str(h_f_color) +
              "HRC=" + str(h_r_color) +
              "H1C=" + str(h_t1_color) +
              "H2C=" + str(h_t2_color) +
              "H3C=" + str(h_t3_color) +
              "CTC=" + str(cb_t_color) +
              "CHC=" + str(cb_h_color) +
              "CSC=" + str(cb_s_color) +
              "MKC=" + str(m_k_color) +
              "WLC=" + str(w_l_color) +
              "WLB=" + str(w_lb_color) +
              "BSC=" + str(button_shadow_color) +
              "BGC=" + str(background_color) +
              "BTC=" + str(button_color) +
              "BKC=" + str(button_key_color) +

              /* SYNTAX HIGHLIGHTING COLORS */
              "RWC=" + parse_str("/RWC=",Global_Str("@SYNTAX_COLORS")) +
              "SYC=" + parse_str("/SYC=",Global_Str("@SYNTAX_COLORS")) +
              "ECC=" + parse_str("/ECC=",Global_Str("@SYNTAX_COLORS")) +
              "SCC=" + parse_str("/SCC=",Global_Str("@SYNTAX_COLORS")) +
              "C1C=" + parse_str("/C1C=",Global_Str("@SYNTAX_COLORS")) +
              "C2C=" + parse_str("/C2C=",Global_Str("@SYNTAX_COLORS")) +
              "NCC=" + parse_str("/NCC=",Global_Str("@SYNTAX_COLORS"))
            );
          }
        }
      }
    }
    insert_Mode = t_insert_mode;
    reg_exp_stat = t_reg_exp_stat;
    switch_win_id(t_int);
    goto LIST;
  }

  set_global_str("!COLOR_BUTTON1","");
  set_global_str("!COLOR_TEMPLATE","");
}
