/*  Multi-Edit Macro for ME 6.x: *** Mike''s WordStar 6.0 ***                    */
/*                                                                              */
/*  * More speed                  * Made by Michael H. Raus                     */
/*  * More features                         Luenener Str.44                     */
/*  * includes help                         D-4708 Kamen                        */
/*  * enter ''memac startup''                 West Germany                        */
/*    to compile and use                    ++49-2307-10910 (Voice)             */
/*    as default startup-macro                                                  */
/*  * All ME keys mappings        for The Wizard''s INN II BBS HST DUAL V.32bis  */
/*    stay active (best               Kamen, Germany                            */
/*    is MULTI-EDIT mapping)          ++49-2307-21968                           */
/*                                    ++49-2307-10012                           */

macro_file startup;

macro set_keys dump {
  set_global_str('mrwsvers','6.2');
  set_global_str('word_delimits','(),.:; #^|026||*<>{}~[]&%$"!/\=-?`+@''');

  /* cmd_to_key(<BS>      ,Back_Space         ,edit);*/
  cmd_to_key(<CtrlA>   ,Word_Left          ,edit);
  cmd_to_key(<CtrlC>   ,Page_Down          ,edit);
  cmd_to_key(<CtrlD>   ,Right              ,edit);
  cmd_to_key(<CtrlE>   ,Up                 ,edit);
  cmd_to_key(<CtrlEnd> ,EOF                ,edit);
  cmd_to_key(<CtrlF>   ,Word_Right         ,edit);
  cmd_to_key(<CtrlG>   ,Del_Char           ,edit);
  cmd_to_key(<CtrlHome>,TOF                ,edit);
  cmd_to_key(<CtrlI>   ,Tab_Right          ,edit);
  cmd_to_key(<CtrlLF>  ,WORD_LEFT          ,edit);
  cmd_to_key(<CtrlPgDn>,NEXT_PAGE_BREAK    ,edit);
  cmd_to_key(<CtrlPgUp>,LAST_PAGE_BREAK    ,edit);
  cmd_to_key(<CtrlR>   ,Page_Up            ,edit);
  cmd_to_key(<CtrlRT>  ,WORD_RIGHT         ,edit);
  cmd_to_key(<CtrlS>   ,Left               ,edit);
  cmd_to_key(<CtrlX>   ,Down               ,edit);
  cmd_to_key(<ShftTAB> ,TAB_LEFT           ,edit);
  cmd_to_key(<ctrlh>   ,Back_Space         ,edit);
  cmd_to_key(<ctrly>   ,del_line           ,edit);
  macro_to_key(<bs>,'startup^smartbs' ,edit);
  macro_to_key(<tab>,'startup^smarttab' ,edit);
  macro_to_key(<CtrlP> ,'startup^quote'    ,edit);
  macro_to_key(<CtrlB> ,'startup^reformat' ,edit);
  macro_to_key(<CtrlK> ,'startup^CtrlK'    ,edit);
  macro_to_key(<CtrlL> ,'startup^RpSearch' ,edit);
  macro_to_key(<CtrlO> ,'startup^CtrlO'    ,edit);
  macro_to_key(<CtrlQ> ,'startup^CtrlQ'    ,edit);
  macro_to_key(<CtrlT> ,'startup^DlWord'   ,edit);
  macro_to_key(<CtrlV> ,'startup^TogglIns' ,edit);
  macro_to_key(<CtrlZ> ,'startup^CtrlZ'    ,edit);
  macro_to_key(<Ctrlj> ,'gotoline'         ,edit);
  macro_to_key(<CtrlU> ,'startup^ctrlu'    ,edit);
  macro_to_key(<ALTF5> ,'startup^userscrn' ,edit);
  macro_to_key(<esc>   ,'meutil3^memenus'  ,edit);
};

macro startup dump {

  run_macro('startup^set_keys');
  make_message('Press key ...');
  put_box(20,7,60,17,blue,white,'WELCOME!',TRUE);
  write('Multi-Edit WordStar Emulation ',25, 8,blue,white);
  write('         written by:'          ,25,10,blue,white);
  write('       Michael H. Raus'        ,25,12,blue,white);
  write('       Lnener Str.44'         ,25,13,blue,white);
  write('         4708 Kamen'           ,25,14,blue,white);
  write('      ++49-2307-21968'         ,25,15,blue,white);
  int i; i = 10;
  while(  not (check_key) & (i > 0)  ) { --i; };
  kill_box;
  load_macro_file('startup.mac');
  make_message('Welcome to Mike''s WordStar V'+global_str('mrwsvers')+'! Use ^K? for help.');
};

macro smartbs trans2
{
  int altccol,i,altcline,newccol,leer;

  push_undo;
  refresh=FALSE;
  altcline=c_line;
  if (altcline>1)
  {
     altccol=c_col;
     if (c_col>1)
     {
        mark_pos; first_word; leer=at_eol & (c_col==1); altccol=c_col; goto_mark;
        mark_pos;
        if (leer)
        {
           altccol=c_col;
           do
           {
             up;
             first_word; newccol=c_col;
             leer=at_eol&(newccol==1);
           }
           while (((leer) | (newccol>=altccol)) & (c_line>1));
           goto_mark; while (c_col>newccol) left;
        }
        else
        if (c_col==altccol)
        {
           do
           {
              up;
              first_word; newccol=c_col; leer=at_eol&(newccol==1);
           }
           while ((leer | (newccol>=altccol)) & (c_line>1));
           goto_mark;
           if (newccol<altccol)
           {
              goto_col(newccol);
              i=altccol-newccol;
              while (i>0)
              {
                 del_char;
                 i--;
              };
           };
        }
        else back_space;
     } else back_space;
  }
  else back_space;
  refresh=TRUE;
  pop_undo;
  redraw;
};

macro smarttab trans2
{
   int afpos,newpos,orginsertmode;

   push_undo; refresh=FALSE;
   afpos=c_col;
   orginsertmode=insert_mode;
   insert_mode=TRUE;
   mark_pos;
   do
   {
      up;
      first_word;
      newpos=c_col;
   } while ((at_eol) & (c_line>1));
   goto_mark;
   if (newpos<=c_col)
   {
      mark_pos;
      up; right;
      if (cur_char==' ') forward_till_not(' ');
      else forward_till(' ');
      right;
      newpos=c_col;
      goto_mark;
      while (c_col<newpos) text(" ");
   }
   else while (newpos>c_col) text(" ");
   insert_mode=orginsertmode;
   refresh=TRUE;
   pop_undo;
   redraw;
}
/*   *** Submenus ***  */

macro CtrlQ trans {

  char Temp_Char;
  str Temp_String;

  Make_Message('^Q'); Read_Key; Make_Message('');
  Temp_Char = Caps(Char(Key1));
  if(  ((Temp_Char == 'S') | (Temp_Char == Char(19)))  ) {
    Goto_Col(1);
    goto endctrlq;
  };
  if(  (Temp_Char == '!')  ) {
    str tempstr;
    tempstr = date;
    text(copy(tempstr,4,2)+'.'+copy(tempstr,1,2)+'.'+copy(tempstr,7,2)+' '+time);
    goto endctrlq;
  };
  if(  (Temp_Char == '?')  ) {
    text(date+' '+time);
    goto endctrlq;
  };
  if(  ((Temp_Char == 'Y') | (Temp_Char == Char(25)))  ) {
    run_macro('deleol');
    goto endctrlq;
  };
  if(  ((Temp_Char == 'D') | (Temp_Char == Char(04)))  ) {
    Eol;
    goto endctrlq;
  };
  if(  ((Temp_Char == 'E') | (Temp_Char == Char(05)))  ) {
    Run_Macro('TopWin');
    goto endctrlq;
  };
  if(  ((Temp_Char == 'X') | (Temp_Char == Char(24)))  ) {
    Run_Macro('BotWin');
    goto endctrlq;
  };
  if(  ((Temp_Char == 'R') | (Temp_Char == Char(18)))  ) {
    Tof;
    goto endctrlq;
  };
  if(  ((Temp_Char == 'C') | (Temp_Char == Char(03)))  ) {
    Eof;
    goto endctrlq;
  };
  if(  (Temp_Char >= '1') & (Temp_Char <= '9')        ) {
    get_mark(ascii(temp_char) - 48);
    make_message('@ marker #'+temp_char);
    read_key;
    make_message('');
    pass_key(key1,key2);
    goto endctrlq;
  } ;
  if(  ((Temp_Char == 'B') | (Temp_Char == Char(02)))  ) {
    Goto_Line(block_line1);
    Goto_Col(block_col1);
    redraw;
    goto endctrlq;
  };
  if(  ((Temp_Char == 'K') | (Temp_Char == Char(11)))  ) {
    Goto_Line(block_line2);
    Goto_Col(block_col2);
    redraw;
    goto endctrlq;
  };
  if(  ((Temp_Char == 'F') | (Temp_Char == Char(06)))  ) {
    Run_Macro('Meutil2^Search');
    goto endctrlq;
  };
  if(  ((Temp_Char == 'A') | (Temp_Char == Char(01)))  ) {
    Run_Macro('Meutil2^S_Repl');
    goto endctrlq;
  };
  if(  ((Temp_Char == 'P') | (Temp_Char == Char(16)))  ) {
    undo;
    goto endctrlq;
  };
  if(  ((Temp_Char == 'L') | (Temp_Char == Char(12)))  ) {

    int cl;

    push_undo; working;
    cl = c_line;
    while(  line_changed  ) {
      undo;
    };
    goto_line(cl);
    redraw;
    pop_undo;
  };
endctrlq:
};

macro CtrlK trans {

  char Temp_Char;
  str Temp_String, FName;
  int Temp_Integer, Status;

  Make_Message('^K'); Read_Key; Make_Message('');
  Temp_Char = Caps(Char(Key1));
  if(  (Temp_Char >= '1') & (Temp_Char <= '9')        ) {
    set_mark(ascii(temp_char) - 48);
    make_message('Set marker #'+temp_char);                    /*  123  */
    read_key;                                                  /*  123  */
    make_message('');                                          /*  123  */
    pass_key(key1,key2);                                       /*  123  */
    goto endctrlk;                                             /*  123  */
  } ;
  if(  ((Temp_Char == 'A') | (Temp_Char == Char(1)))  ) {
    push_undo;
    if(  block_stat != 2  ) {
       make_message('No column block marked');
       read_key;
       make_message('');
       pass_key(key1,key2);
		} else {

      int erg,tval,cx,cy,er;

      cx = c_col; cy = c_line;
      erg = 0;
      refresh = false; working;
      goto_line(block_line1);
      while(  c_line <= block_line2  ) {
        if(  val(tval,remove_space(copy(get_line,block_col1,block_col2-block_col1+1))) == 0  ) {
          erg = erg + tval;
        };
        down;
      };
      refresh = true;
      goto_line(cy);
      goto_col(cx);
      make_message('Result '+str(erg)+' cursor to position, enter to paste, ESC to cancel');
      key1 = 0;
      while(  (key1 != 13) & not ((key1 == 27) & (key2 == 1))  ) {
        read_key;
        if(  ((Key1 == 24) & (key2 == 45)) | ((key1 == 0) & (key2 == 80))  ) {
          down;
				} else {
        if(  ((Key1 == 5) & (key2 == 18)) | ((key1 == 0) & (key2 == 72))  ) {
          up;
				} else {
        if(  ((Key1 == 0) & (key2 == 132)) | ((Key1 == 0) & (key2 == 71))  ) {
          home;
				} else {
        if(  ((Key1 == 19) & (key2 == 31)) | ((Key1 == 0) & (key2 == 75))  ) {
          left;
				} else {
        if(  ((Key1 == 4) & (key2 == 32)) | ((Key1 == 0) & (key2 == 77))  ) {
          right;
				} else {
        if(  key1 == 13  ) {
          text(str(erg));
        };
        };
        };
        };
        };
        };
      };
      make_message('');
    };
    redraw;
    pop_undo;
    goto endctrlk;
  };
  if(  ((Temp_Char == 'Y') | (Temp_Char == Char(25)))  ) {
    Delete_Block;
    goto endctrlk;
  };
  if(  ((Temp_Char == 'I') | (Temp_Char == Char(9))) |
     ((Temp_Char == 'U') | (Temp_Char == Char(21)))  ) {
    int c,ax,ay;

    refresh = false;
    push_undo;
    if(  block_stat != 0  ) {
       ax = c_col; ay = c_line;
       c = block_line1;
       while(  ((block_stat != 2) & (c <= block_line2)) |
             ((block_stat == 2)  & (c <= block_line2))  ) {
         goto_line(c);
         if(  block_stat == 2  ) {
            goto_col(block_col1);
				 } else {
            goto_col(1);
         };
         if(  ((Temp_Char == 'U') | (Temp_Char == Char(21)))  ) {
            del_char;
				 } else {
            text(' ');
         };
         c = c +1;
       };
       goto_col(ax); goto_line(ay);
    };
    pop_undo;
    refresh = true;
    redraw;
    goto endctrlk;
  };
  if(  ((Temp_Char == 'D') | (Temp_Char == Char(04)))  ) {
    if(  file_name == '?No-File?'  ) {
      run_macro('meutil1^savefile');
		} else {
      run_macro('meutil1^savefile /NP=1');
    };
    goto endctrlk;
  };
  if(  ((Temp_Char == 'Q') | (Temp_Char == Char(17)))  ) {
    str fn;
    int c,d,e;

    fn = file_name;
    run_macro('delwin');
    run_macro('winmenu');
    if(  file_name == '?No-File?'  ) {
      rest_dos_screen;
      quit(0);
		} else {
      make_message(fn+' gone.');
      refresh = true;
      new_screen;
    };
    goto endctrlk;
  };
  if(  ((Temp_Char == 'S') | (Temp_Char == Char(19)))  ) {
    if(  not(file_changed)  ) {
      make_message('No change - use ^KD to force save.');
      read_key;
      make_message('');
      pass_key(key1,key2);
		} else {
      if(  file_name == '?No-File?'  ) {
        run_macro('meutil1^savefile');
			} else {
        run_macro('meutil1^savefile /NP=1');
      };
    };
    goto endctrlk;
  };
  if(  ((Temp_Char == 'T') | (Temp_Char == Char(20)))  ) {

    int x,y;
    str aktword;

    push_undo;
    refresh = FALSE;
    x = c_col; y = c_line;
    str_block_begin; word_right;
    block_end;
    goto_col(x); goto_line(y);
    refresh = TRUE;
    redraw;
    pop_undo;
    goto endctrlk;
  };
  if(  ((Temp_Char == 'X') | (Temp_Char == Char(24)))  ) {
    if(  (file_name == '?No-File?')  ) {
      make_message('File has no name. Name file first or use ^KQ.');
      read_key;
      make_message('');
      pass_key(key1,key2);
		} else {
      save_file;
      run_macro('exit');
    };
  };
  if(  ((Temp_Char == 'P') | (Temp_Char == Char(16)))  ) {
    Run_Macro('meutil3^Print');
    goto endctrlk;
  };
  if(  ((Temp_Char == 'N') | (Temp_Char == Char(14)))  ) {
    col_block_begin;
    goto endctrlk;
  };
  if(  ((Temp_Char == 'B') | (Temp_Char == Char(02)))  ) {
    str_block_begin;
    goto endctrlk;
  };
  if(  ((Temp_Char == 'K') | (Temp_Char == Char(11)))  ) {
    block_end;
    goto endctrlk;
  };
  if(  ((Temp_Char == 'H') | (Temp_Char == Char(08)))  ) {
    block_off;
    goto endctrlk;
  };
  if(  ((Temp_Char == 'C') | (Temp_Char == Char(03)))  ) {
     copy_block;
    goto endctrlk;
  };
  if(  ((Temp_Char == 'V') | (Temp_Char == Char(22)))  ) {
     move_block;
    goto endctrlk;
  };
  if(  ((Temp_Char == 'W') | (Temp_Char == Char(23)))  ) {
     Run_Macro('meutil1^saveblck');
    goto endctrlk;
  };
  if(  ((Temp_Char == 'R') | (Temp_Char == Char(18)))  ) {

    rm('meutil1^splice');
    goto endctrlk;
  };
  if(  (Temp_Char == '?')  ) {
    int y;
    y = screen_length / 2-10;
    put_box(1,y,81,y + 22,blue,white,'Mike''s Quick-WordStar V'+global_str('mrwsvers')+' Reference',TRUE);
    write(' ÚÄÄÄÄÄÄÄÄ^Q ^W ^E ^R ^T ^Z ^U ^I ^O ^P * ENHANCED SPEED AND FEATURES       ',02,y + 1 ,blue,white);
    write(' ³ÚÄUndoÄÄÂÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ     ÄÄÄ¿* ALL ME KEYS ALSO ACTIVE           ',02,y + 2 ,blue,white);
    write(' ³ ^U Undo  ^A ^S ^D ^F ^G ^H ^J ^K ^L  ³* ALL OTHER KEYS ARE STANDARD WS    ',02,y + 3 ,blue,white);
    write(' ³ ^R Redo                             ³* NOTE:^Y/^Z ARE GERMAN KEYBOARD!   ',02,y + 4 ,blue,white);
    write(' ³           ^Y ^X ^C ^V ^B ^N ^M ³     ³* $=other authors'' material        ',02,y + 5 ,blue,white);
    write(' ÃÄQuickÄÄÄÄÄÄÄÄÄ¿ÚÄBlocksÄÄÄÄÄÄÄÄÁÄÄÄÄ¿³* ?=not yet 100% ok                 ',02,y + 6 ,blue,white);
    write('  ^R BOF ^C BOF    ^A Add column block  ³* ^T can wrap lines * ESC call menu ',02,y + 7 ,blue,white);
    write('  ^S BOL ^D EOL    ^I Indent block      ³* ^P Quote char     * ^J Jump line  ',02,y + 8 ,blue,white);
    write('  ^E TOS ^X BOS    ^U Unindent block    ³* ^N Copy char      * ^B Reformat   ',02,y +09 ,blue,white);
    write('  ^F Fnd ^A Repl   ^Y Del     ^C Copy   ³* ALT-F5 Show Userscreen            ',02,y +10 ,blue,white);
    write('  ^Y Del EOL       ^H Un/Hide ^V Move   ÃÄOnscreenÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ¿ ',02,y +11 ,blue,white);
    write('  ^B Block begin   ^T Word ^Q Quit ed    ^S Sort (can use ^KN borders)       ',02,y +12 ,blue,white);
    write('  ^K Block end     ^W Wrte ^R Read (*,?) ^R right margin   ^L left margin    ',02,y +13 ,blue,white);
    write('  ^P?Last pos      ^D Forced save        ^K$Calendar       ^N ANSI Table     ',02,y +14 ,blue,white);
    write('  ^L Restore line  ^S Save file          ^G$CGA Colors                       ',02,y +15 ,blue,white);
    write('  1..9 Go marker   ^P Print file         ^B Block justify (use ^OR/L or ^KN) ',02,y +16 ,blue,white);
    write('  ! Date/Time EUR  ^1..9 Set marker      ^C Center line (uses ^OR/L)         ',02,y +17 ,blue,white);
    write('  ? Date/Time US   ^X Quit    ^I Indent  ^W Word wrap toggle                 ',02,y +18 ,blue,white);
    write('                   ^N Mark column        [  Comment line (PASCAL/C)          ',02,y +19 ,blue,white);
    write('                   ?  This screen                                            ',02,y +20 ,blue,white);
    read_key;
    kill_box;

  };
endctrlk:
};

macro CtrlO trans {

  char Temp_Char;

  Make_Message('^O');
  Read_Key;
  Make_Message('');
  if(  (Key1 != 0)  ) {
		Temp_Char = Caps(Char(Key1));
    if(  ((Temp_Char == 'S') | (Temp_Char == Char(19)))  ) {
      run_macro('startup^ctrlos');
      goto endctrlo;
    };
    if(  ((Temp_Char == 'G') | (Temp_Char == Char(7)))  ) {
      run_macro('startup^cga');
      goto endctrlo;
    };
    if(  ((Temp_Char == 'R') | (Temp_Char == Char(18)))  ) {
      Run_Macro('wordstar^RightMar');
      goto endctrlo;
    };
    if(  ((Temp_Char == 'F') | (Temp_Char == Char(6)))  ) {
      run_macro('startup^ctrlof');
      goto endctrlo;
    };
    if(  ((Temp_Char == 'L') | (Temp_Char == Char(12)))  ) {
      Run_Macro('wordstar^LeftMar');
      goto endctrlo;
    };
    if(  ((Temp_Char == 'K') | (Temp_Char == Char(11)))  ) {
      run_macro('startup^kal');
      goto endctrlo;
    };
    if(  ((Temp_Char == 'B') | (Temp_Char == Char(2)))  ) {
      run_macro('startup^ctrlob');
      goto endctrlo;
    };
    if(  (temp_char == 'C') | (temp_char == char(3))     ) {
      int Temp_Integer,Temp_Insert;

      Temp_Insert = Insert_Mode;
      Insert_Mode = TRUE;
      if(  (Get_Line == '')  ) {
         goto endctrlo;
      };
      put_line(remove_space(get_line));
      First_Word;
      Temp_Integer = C_Col;
      Eol;
      if(  (C_Col - Temp_Integer > global_int('right_margin'))  ) {
        goto endctrlo;
      };
      Temp_Integer = (((global_int('right_margin') - (C_Col - Temp_Integer)) + global_int('Left_Margin')) / 2);
      First_Word;
      if(  C_Col < Temp_Integer  ) {
        Text(Copy('                                                                                            ',
                  1,Temp_Integer - C_Col));
      };
      if(  (C_Col > Temp_Integer)  ) {
        Temp_Integer = C_Col - Temp_Integer;
        Goto_Col(C_Col - Temp_Integer);
        Del_Chars(Temp_Integer);
      };
      goto endctrlo;
    };
    if(  (temp_char == 'W') | (temp_char == char(23))  ) {
      Wrap_Stat = NOT(Wrap_Stat);
      goto endctrlo;
    };
    if(  (temp_char == 'N') | (temp_char == char(14))  ) {
      Run_Macro('startup^ansi') ;
      goto endctrlo;
    } ;
    if(  temp_char == '['  ) {
      Run_Macro('startup^Comment') ;
    } ;
  };
endctrlo:
  load_macro_file('startup.mac');
};

macro Ctrlu trans {

  char Temp_Char;

  Make_Message('^U');
  Read_Key;
  Make_Message('');
  if(  (Key1 != 0)  ) {
		Temp_Char = Caps(Char(Key1));
    if(  ((Temp_Char == 'U') | (Temp_Char == Char(21)))  ) {
      undo;
      goto endctrlu;
    };
    if(  ((Temp_Char == 'R') | (Temp_Char == Char(18)))  ) {
      redo;
    };
  };
endctrlu:
};

/*   *** Utilities ***  */

macro userscrn dump {

  int altvidmode;

  altvidmode = video_mode;
  rest_dos_screen;
  while(  not (check_key)  ) {
  };
  if(  video_mode != altvidmode  ) {
    set_video_mode(altvidmode);
  };
  new_screen;
};

macro del_lin trans {

  int ac;

  refresh = false;
  ac = c_col;
  goto_col(1);
  while(  not(at_eol)  ) {
    del_char;
  };
  del_char;
  goto_col(ac);
  refresh = true;
};

macro quote trans {
  make_message('^P');
  read_key;
  if(  key1 != 0  ) {
    text(char(key1));
	} else {
    beep;
  };
  make_message('');
};

macro ctrlob trans {
  int sw,stoi,x,r,l,le,altins,al,y,maxline,alm,arm,ay;

  real p;

  if(  block_stat == 2  ) {
    alm = global_int('left_margin');
    arm = global_int('right_margin');
    set_global_int('left_margin',block_col1);
    set_global_int('right_margin',block_col2);
  };
  if(  (global_int('left_margin') > 80) | (global_int('right_margin') > 174) |
      ((global_int('right_margin') - global_int('left_margin')) < 10)  ) {
    make_message('Margins out of bounds');
    goto endctrlob;
  };
  push_undo;
  working;
  x = c_col; ay = c_line;
  refresh = false;
  while(  length(get_line) > 0  ) {
    up;
  };
  down;
  y = c_line; altins = insert_mode; sw = 0;
  insert_mode = TRUE;
  right_margin = global_int('right_margin') - global_int('left_margin') + 1;
  make_message('Just: PASS 1#2');
  while(  not(at_eof)  ) {
      put_line(remove_space(get_line));
      l = length(get_line);
      if(  l == 0  ) { goto goon; };
      while(  l < right_margin  ) {
        down; eol;
        if(  c_col == 1  ) { goto goon; };
        up; eol; right; del_char;
        put_line(remove_space(get_line));
        l = length(get_line);
      };
      al = c_line;
      word_wrap_line(true,false);
      p = real_i(al + 1 - y) / real_i(c_line - y) * 100.0;
      make_message('Just: PASS 1#2 '+str(int_r(p))+'% complete ...');
      goto_line(al);
      put_line(remove_space(get_line));
      down;
  };
goon:
  maxline = c_line - y;
  goto_line(y);
  while(  length(get_line) > 0  ) {
    p = real_i(c_line - y) / real_i(maxline) * 100.0;
    make_message('Just: PASS 2#2 '+str(int_r(p))+'% complete ...');
    eol;
    if(  c_col > (right_margin / 2)   ) {
      l = 1; r = c_col; le = length(get_line); stoi = 0;
      while(  (right_margin > le) & not (stoi)  ) {
            if(  sw  ) {
                goto_col(r);
                if(  search_bwd(' [~ ]',1)  ) {
                  r = c_col-1; replace(' &');
								} else {
                  if(  r == le  ) { ++stoi;
                  } else { r = le;
                  };
                };
						} else {
                goto_col(l);
                if(  search_fwd('[~ ] ',1)  ) {
                  l = c_col+2; replace('& ');
								} else {
                  if(  l == 1  ) { ++stoi;
                  } else { l = 1;
                  };
                };
            };
            sw = not (sw); le = length(get_line);
      };
    };
    goto_col(1);
    Text(Copy('                                                                                 ',
          1,global_int('left_margin') - 1));
    down;
  };
  insert_mode = altins;
  right_margin = global_int('right_margin');
  goto_col(x); goto_line(ay);
  refresh = TRUE;
  redraw;
  make_message('');
  pop_undo;
  if(  block_stat == 2  ) {
    set_global_int('left_margin',alm);
    set_global_int('right_margin',arm);
  };
  endctrlob:
};

macro ctrlos trans {

  int i,j,k,m,n,altundo,anflin,endlin,anfcol,endcol;
  str orgline1,orgline2;

  push_undo;
  working;
  make_message('Sorting ...');
  refresh = FALSE;
  altundo = undo_stat;
  if(  block_stat == 2  ) {
      anflin = block_line1; endlin = block_line2;
      anfcol = block_col1; endcol = block_col2;
	} else {
      anflin = 1; eof; endlin = c_line;
      anfcol = 1; endcol = 254;
  };
  n = endlin - anflin+1;
  if(  n > 100  ) {
      undo_stat = FALSE;
      make_message('Sort: Undo turned off while sorting to prevent swapping.');
	} else {
      undo_stat = TRUE;
  };
  m = n / 2;
  while(  m > 0  ) {
    i = 1;
    while(  i <= (n - m)  ) {
      j = i;
      while(  (j > 0 )  ) {
        goto_line(j+anflin-1); orgline1 = get_line;
        goto_line(j+m+anflin-1); orgline2 = get_line;
        if(  copy(orgline1,anfcol,endcol) >
            copy(orgline2,anfcol,endcol)  ) {
            goto_line(j+anflin-1); put_line(orgline2);
            goto_line(j+m+anflin-1); put_line(orgline1);
				} else {
          j = 0;
        };
        j = j - m;
      };
      i = i + 1;
    };
    m = m / 2;
  };
  undo_stat = altundo;
  goto_line(anflin); goto_col(anfcol);
  refresh = TRUE;
  pop_undo;
  redraw;
};

macro ctrlof dump {

  str menu_str;
  char top_ch,bot_ch,rt_ch,lt_ch,ul_ch,ur_ch,ll_ch,lr_ch;
  int i,width,height,orig_col,orig_line,orig_ins,select;

  push_undo;
  if(  (BLOCK_STAT != 2)  ) {
    make_message('No Marked Block');
    beep;
    goto end_of_mac;
  };
  height = block_line2 - block_line1;
  width  = block_col2  - block_col1;
  if(  (not (width) | not (height))  ) {
      make_message('Cannot Box a single high or wide block');
      beep; delay(5);
    goto end_of_mac;
  };
  working;
  orig_col = C_COL;
  orig_line = C_LINE;
  orig_ins = insert_mode;
  insert_mode = FALSE;
  menu_str = 'ºAº(BX)ºB³(BX)ºCº(BX)³Dº(BX)ºEº(BX)³F³(BX)³Gº(BX)³Hº(BX)ºI³(BX)ºJ³(BX)³K³(BX)³Lº(BX)³M³(BX)ºN³(BX)³O³(BX)ºPº(BX)';
  put_box(1,4,68,9,m_t_color,m_B_color,'Box Type',TRUE);
  write('ÖÄ· ÉÍ¸ ÉÍ» ÕÍ» ÖÄ· ÕÍ¸ ÕÍ» ÚÄ· ÖÄ¿ ÉÍ¸ ÕÍ¸ ÚÄ· ÚÄ¿ ÖÄ¿ ÚÄ¿ ÉÍ»',3,5,m_t_color,m_b_color);
  write('ÈÍ¼ ÈÍ¾ ÓÄ½ ÔÍ¼ ÓÄ½ ÔÍ¾ ÀÄ½ ÔÍ¼ ÈÍ¾ ÓÄÙ ÀÄÙ ÀÄ½ ÔÍ¾ ÓÄÙ ÀÄÙ ÈÍ¼',3,7,m_t_color,m_b_color);
  run_macro('xmenu /X=3 /Y=6 /B=0 /T=0 /M='+menu_str);
  kill_box;
  select = return_int;

  if(  (select == 0)  ) {
    goto end_of_mac;
  };

  refresh = FALSE;

  menu_str = 'Ä·º¼ÍÈºÖÍ¸³¾ÍÈºÉÍ»º½ÄÓºÉÍ»º¼ÍÔ³ÕÄ·º½ÄÓºÖÍ¸³¾ÍÔ³ÕÍ»º½ÄÀ³ÕÄ·º¼ÍÔ³ÚÄ¿³¾ÍÈºÖÍ¸³ÙÄÓºÉÍ¸³ÙÄÀ³ÕÄ·º½ÄÀ³ÚÄ¿³¾ÍÔ³ÚÄ¿³ÙÄÓºÖÄ¿³ÙÄÀ³ÚÍ»º¼ÍÈºÉ';
  menu_str = copy(menu_str,1 + ((select - 1) * 8),8);

  top_ch =  copy(menu_str,1,1);
  ur_ch  =  copy(menu_str,2,1);
  rt_ch  =  copy(menu_str,3,1);
  lr_ch  =  copy(menu_str,4,1);
  bot_ch =  copy(menu_str,5,1);
  ll_ch  =  copy(menu_str,6,1);
  lt_ch  =  copy(menu_str,7,1);
  ul_ch  =  copy(menu_str,8,1);

  goto_line(block_line1);
  goto_col(block_col1);
  text(ul_ch);
  i = 2;
  while(  (i <= width)  ) {
    text(top_ch);
    i = i+1;
  };
  text(ur_ch);

  goto_line(block_line2);
  goto_col(block_col1);
  text(ll_ch);
  i = 2;
  while(  (i <= width)  ) {
    text(bot_ch);
    i = i+1;
  };
  text(lr_ch);

  goto_line(block_line1+1);
  i = 2;
  while(  (i <= height)  ) {
    goto_col(block_col1);
    text(lt_ch);
    goto_col(block_col2);
    text(rt_ch);
    down;
    i = i+1;
  };

  insert_mode = orig_ins;
  goto_line(orig_line); goto_col(orig_col);
  refresh = TRUE;
  redraw;
  pop_undo;
end_of_mac:
};

macro Copy_Ch to <ctrln> {
  char Ch;
  if(  C_Line > 1  ) {
    Refresh = 0 ;
    Up ;
    if(  Not (At_EOL)  ) {
      Ch = Cur_Char ;
      Down ;
      Text(Ch) ;
		} else {
      Down ;
    } ;
    Refresh = TRUE ;
    redraw;
  } ;
} ;

macro dlword trans {
  str aktword;
  int cc,cl;

  push_undo;
  refresh = FALSE;
  cc = c_col; cl = c_line;
  aktword = get_word(global_str('word_delimits'));
  goto_line(cl); goto_col(cc);
  if(  (length(aktword)==0)  ) {
loop01:
    del_char;
    if(  not (at_eof) &
        ((cur_char == char(32)) | at_eol)  ) { goto loop01;
    };
	} else {
loop02:
    del_char;
    if(  (pos(cur_char,global_str('word_delimits')) == 0) &
        not (at_eof) &
        not (at_eol)  ) { goto loop02;
    };
  };
  refresh = TRUE;
  redraw;
  pop_undo;
};

macro reformat trans {
  word_wrap_line(true,true);
};

macro togglins {
  insert_mode = not (insert_mode);
};

macro RpSearch trans {
	if(  Global_Str('Search_Str') != ''  ) {
		Run_Macro('Meutil2^RepSrch');
	};
};

macro CtrlW {
	Push_Undo;
	Run_Macro('ScrollDn');
	if(  WhereY < (Win_Y2 - 1)  ) {
		Down;
	};
	Pop_Undo;
};

macro CtrlZ {
	Push_Undo;
	Run_Macro('ScrollUp');
	if(  WhereY > (Win_Y1 + 1)  ) {
		Up;
	};
	Pop_Undo;
};

/* ***************************************************************************** */
/*                                 MULTI-EDIT MACRO                              */
/*  Name:Comment                                     Timestamp:07/25/88 03:22am  */
/*  Created by: Herb Martin                                                      */
/*  Description: Encloses current line in comments, align if possible.           */
/* ***************************************************************************** */
macro Comment Trans {

  int SI,cc;

  push_undo; cc = c_col; Goto_Col(1); SI = Insert_Mode; Insert_Mode = TRUE;
  if(  (xpos('.SRC',file_name,1) > 0) |  (xpos('.PAS',file_name,1) > 0)  ) {
     Text('{ ') ;
	} else {
    if(  (xpos('.C',file_name,1) > 0) | (xpos('.H',file_name,1) > 0)  ) {
      text('/*');
    };
  };
  EOL;
  if(  C_Col < 78  ) {
    Goto_Col(78) ;
  } ;
  if(  (xpos('.SRC',file_name,1) > 0) |  (xpos('.PAS',file_name,1) > 0)  ) {
     Text('} ') ;
  } else { if(  (xpos('.C',file_name,1) > 0) | (xpos('.H',file_name,1) > 0)  ) {
     text('*/');
  };
  };
  Insert_Mode = SI ;
  Down; goto_col(cc);
};

/*   *** Programs ***  */

/* ****************************************************************************** */
/*                                 MULTI-EDIT MACRO                               */
/*                                                                                */
/* Name: KAL                                   Timestamp: 06/04/88 02:46pm        */
/*                                                                                */
/* Description: Shows calander on screen       Contributed by Dick Botbyl         */
/*                                                                                */
/* ****************************************************************************** */

macro kal dump {

  str cal_top,cal_day,cal_bar,cal_inp,
          cal_lin,cal_lli,cal_mon,cal_bot,
          mon_str,day_str,year_str,long_mo,
          sho_day_str,cal_key;
  int mon_num,day_o_wk,short_yr,temp_value,num_days,start_day,mon_type,
          long_yr,strt_ln,sho_day,Key_code,start_day_out;



  cal_top = '³                                  º';
  cal_day = '³ Mon  Tue  Wed  Thu  Fri  Sat Sun º';
  cal_bar = 'ÆÍÍÍÍÑÍÍÍÍÑÍÍÍÍÑÍÍÍÍÑÍÍÍÍÑÍÍÍÍÑÍÍÍÍ¹';
  cal_inp = '³    ³    ³    ³    ³    ³    ³    º';
  cal_lin = 'ÃÄÄÄÄÅÄÄÄÄÅÄÄÄÄÅÄÄÄÄÅÄÄÄÄÅÄÄÄÄÅÄÄÄÄ¶';
  cal_lli = 'ÃÄÄÄÄÅÄÄÄÄÅÄÄÄÄÁÄÄÄÄÁÄÄÄÄÁÄÄÄÄÁÄÄÄÄ¶';
  cal_mon = '³    ³    ³                        º';
  cal_bot = 'ÆÍÍÍÍÏÍÍÍÍÏÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍ¹';
  cal_key = '³   Months    Years     [End]';
  mon_str  = COPY (DATE,1,2);
  day_str  = COPY (DATE,4,2);
  year_str = COPY (DATE,7,2);



  if(  VAL(temp_value, mon_str)  == 0  ) {
    mon_num = temp_value;
  };
  if(  VAL(temp_value, day_str)  == 0  ) {
    day_o_wk = temp_value;
  };
  if(  VAL(temp_value, year_str) == 0  ) {
    short_yr = temp_value;
  };
  long_yr = short_yr + 1900;



  put_box(21,4,58,22,lightgray,back_color,'Calendar',TRUE);

BEGIN_TO:
  write(cal_top,21, 5,lightgray,back_color);
  write(cal_day,21, 6,lightgray,back_color);
  write(cal_bar,21, 7,lightgray,back_color);
  write(cal_inp,21, 8,lightgray,back_color);
  write(cal_lin,21, 9,lightgray,back_color);
  write(cal_inp,21,10,lightgray,back_color);
  write(cal_lin,21,11,lightgray,back_color);
  write(cal_inp,21,12,lightgray,back_color);
  write(cal_lin,21,13,lightgray,back_color);
  write(cal_inp,21,14,lightgray,back_color);
  write(cal_lin,21,15,lightgray,back_color);
  write(cal_inp,21,16,lightgray,back_color);
  write(cal_lli,21,17,lightgray,back_color);
  write(cal_mon,21,18,lightgray,back_color);
  write(cal_bot,21,19,lightgray,back_color);
  write(cal_key,21,20,lightgray,back_color);

  if(  short_yr < 72  ) {
    short_yr = short_yr +28;
  };
  if(  short_yr > 99  ) {
    short_yr = short_yr -28;
  };

  if(  short_yr == 72  ) {
    mon_type =  13;
  };
  if(  short_yr == 73  ) {
    mon_type =   1;
  };
  if(  short_yr == 74  ) {
    mon_type =   2;
  };
  if(  short_yr == 75  ) {
    mon_type =   3;
  };
  if(  short_yr == 76  ) {
    mon_type =  11;
  };
  if(  short_yr == 77  ) {
    mon_type =   7;
  };
  if(  short_yr == 78  ) {
    mon_type =   8;
  };
  if(  short_yr == 79  ) {
    mon_type =   1;
  };
  if(  short_yr == 80  ) {
    mon_type =  09;
  };
  if(  short_yr == 81  ) {
    mon_type =   4;
  };
  if(  short_yr == 82  ) {
    mon_type =   5;
  };
  if(  short_yr == 83  ) {
    mon_type =   6;
  };
  if(  short_yr == 84  ) {
    mon_type =  14;
  };
  if(  short_yr == 85  ) {
    mon_type =   2;
  };
  if(  short_yr == 86  ) {
    mon_type =   3;
  };
  if(  short_yr == 87  ) {
    mon_type =   4;
  };
  if(  short_yr == 88  ) {
    mon_type =  12;
  };
  if(  short_yr == 89  ) {
    mon_type =   7;
  };
  if(  short_yr == 90  ) {
    mon_type =   1;
  };
  if(  short_yr == 91  ) {
    mon_type =   2;
  };
  if(  short_yr == 92  ) {
    mon_type =  10;
  };
  if(  short_yr == 93  ) {
    mon_type =   5;
  };
  if(  short_yr == 94  ) {
    mon_type =   6;
  };
  if(  short_yr == 95  ) {
    mon_type =   7;
  };
  if(  short_yr == 96  ) {
    mon_type =  08;
  };
  if(  short_yr == 97  ) {
    mon_type =   3;
  };
  if(  short_yr == 98  ) {
    mon_type =   4;
  };
  if(  short_yr == 99  ) {
    mon_type =   5;
  };
  num_days = 31;
  if(  mon_num == 1  ) {
    mon_str = 'January';
    start_day = 1;
  };
  if(  mon_num == 2  ) {
    start_day = 4;
    num_days = 28;
    mon_str = 'February';
  };
  if(  mon_num == 3  ) {
    mon_str = 'March';
    start_day = 4;
  };
  if(  mon_num == 4  ) {
    num_days = 30;
    mon_str = 'April';
    start_day = 7;
  };
  if(  mon_num == 5  ) {
    mon_str = 'May';
    start_day = 2;
  };
  if(  mon_num == 6  ) {
    num_days = 30;
    mon_str = 'June';
    start_day = 5;
  };
  if(  mon_num == 7  ) {
    mon_str = 'July';
    start_day = 7;
  };
  if(  mon_num == 8  ) {
    mon_str = 'August';
    start_day = 3;
  };
  if(  mon_num == 9  ) {
    num_days = 30;
    mon_str = 'September';
    start_day = 6;
  };
  if(  mon_num == 10  ) {
    mon_str = 'October';
    start_day = 1;
  };
  if(  mon_num ==11  ) {
    num_days = 30;
    mon_str = 'November';
    start_day = 4;
  };
  if(  mon_num ==12  ) {
    mon_str = 'December';
    start_day = 6;
  };

  if(  mon_type > 7  ) {
    if(  mon_num > 2  ) {
      start_day = start_day + 1;
    };
  };
  if(  mon_type > 7  ) {
    mon_type = mon_type -7;
  };
  start_day = start_day + mon_type - 1;
  if(  start_day > 7  ) {
    start_day = start_day - 7;
  };


  write(mon_str,38,18,lightgray,darkgray);
  write(STR(long_yr),49,18,lightgray,darkgray);

  strt_ln = 8;
  sho_day = 1;
  start_day_out = (start_day * 5) + 18;
  while(  sho_day <= num_days  ) {
    if(  start_day_out > 57  ) {
      start_day_out = start_day_out - 35;
      strt_ln = strt_ln +2;
    };
    sho_day_str = STR(sho_day);
    if(  length (sho_day_str) == 1  ) {
      sho_day_str = ' ' + sho_day_str;
    };
    write(sho_day_str,start_day_out,strt_ln,lightgray,white);
    sho_day = sho_day + 1;
    start_day_out = start_day_out + 5;
  };

SWITCH:
  Read_key;
    if(  Key1 == 0  ) {
      Key_code = Key2;
      if(  Key_code == 77  ) {
        mon_num = mon_num + 1;
        if(  mon_num == 13  ) {
          mon_num = 1;
          short_yr = short_yr +1;
          long_yr = long_yr + 1;
        };
        GOTO BEGIN_TO;
      };
      if(  Key_code == 75  ) {
        mon_num = mon_num - 1;
        if(  mon_num == 0  ) {
          mon_num = 12;
          short_yr = short_yr -1;
          long_yr = long_yr - 1;
        };
        GOTO BEGIN_TO;
      };
      if(  Key_code == 72  ) {
        short_yr = short_yr + 1;
        long_yr = long_yr + 1;
        GOTO BEGIN_TO;
      };
      if(  Key_code == 80  ) {
        short_yr = short_yr - 1;
        long_yr = long_yr - 1;
        GOTO BEGIN_TO;
      };
      if(  Key_code == 73  ) {
        short_yr = short_yr + 5;
        long_yr = long_yr + 5;
        GOTO BEGIN_TO;
      };
      if(  Key_code == 81  ) {
        short_yr = short_yr - 5;
        long_yr = long_yr - 5;
        GOTO BEGIN_TO;
      };
      if(  Key_code == 79  ) {
        mon_num = mon_num + 1;
        GOTO FINI;
      };
		} else {
      if(  key1 == 27  ) {
        mon_num = mon_num + 1;
        GOTO FINI;
      };
    };
    GOTO SWITCH;

FINI:
  Kill_box;
end_of_mac:
};

/*  ----------------------------------------------------------------------------  */

macro ansi dump {

  str clr_top, clr_str, clr_bot, clr_key, clr_kod, ans_def;
  int clr_bak, clr_num, clr_col, clr_row, clr_for;
  int flop, key_code;

  clr_top = '  30  34  32  36  31  35  33  37 ';
  clr_bot = 'ÆÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍ¹';
  clr_kod = '    Color Sequence =  [0;3?;4?m';
  clr_key = ' [PgUp]    [PgDn]    [End]';

  flop = 0;

  put_box(39,4,78,18,lightgray,back_color,' ANSI Color Chart ',TRUE);
  write(clr_top,42,5,lightgray,back_color);
  write(clr_bot,39,14,lightgray,back_color);
  write(clr_kod,40,15,lightgray,back_color);
  write(clr_key,43,16,lightgray,back_color);
  clr_num = 40;
  clr_col = 40;
  clr_row =  6;
  write('40',40,6,lightgray,back_color);
  write('44',40,7,lightgray,back_color);
  write('42',40,8,lightgray,back_color);
  write('46',40,9,lightgray,back_color);
  write('41',40,10,lightgray,back_color);
  write('45',40,11,lightgray,back_color);
  write('43',40,12,lightgray,back_color);
  write('47',40,13,lightgray,back_color);
  GOTO NO_FLASH;

IN_KEY:
  Read_key;
    if(  Key1 == 27  ) {                             /* Esc to Quit */
      GOTO FINI;
    };
    if(  Key1 == 0  ) {
      Key_code = Key2;
      if(  Key_code == 79  ) {                       /* END to Quit */
        GOTO FINI;
      };
      if(  Key_code == 73  ) {                       /* PG UP other screens */
        flop = flop - 1;
      };
      if(  Key_code == 81  ) {                       /* PG DN other screens */
        flop = flop + 1;
      };
      if(  flop == 4  ) {
        flop = 0;
      };
      if(  flop == -1  ) {
        flop = 3;
      };
      if(  flop == 0  ) {
        ans_def = '0;3?;4?m  ';
        write(ans_def,64,15,lightgray,back_color);
        GOTO NO_FLASH;
      };
      if(  flop == 1  ) {
        ans_def = '0;3?;4?;5m ';
        write(ans_def,64,15,lightgray,back_color);
        GOTO FLASH;
      };
      if(  flop == 2  ) {
        ans_def = '1;3?;4?m   ';
        write(ans_def,64,15,lightgray,back_color);
        GOTO NO_FLASH_BOLD;
      };
      if(  flop == 3  ) {
        ans_def = '1;3?;4?;5m';
        write(ans_def,64,15,lightgray,back_color);
        GOTO FLASH_BOLD;
      };
    };
    GOTO IN_KEY;

NO_FLASH:
  clr_col = 43;
  clr_row =  6;
  clr_num = 30;
  clr_bak =  0;
  clr_for =  0;
  while(  clr_row < 14  ) {
      clr_str = '  ';
      write(clr_str,clr_col,clr_row,clr_bak,clr_for);
      clr_num = clr_num +1;
      clr_for = clr_for +1;
      clr_col = clr_col + 4;
      if(  clr_col > 73  ) {
        clr_col = 43;
        clr_row = clr_row + 1;
        clr_for = 0;
        clr_bak = clr_bak + 1;
        clr_num = 30;
      };
  };
  GOTO IN_KEY;

NO_FLASH_Bold:
  clr_col = 43;
  clr_row =  6;
  clr_num = 30;
  clr_bak =  0;
  clr_for =  8;
  while(  clr_row < 14  ) {
      clr_str = '  ';
      write(clr_str,clr_col,clr_row,clr_bak,clr_for);
      clr_num = clr_num +1;
      clr_for = clr_for +1;
      clr_col = clr_col + 4;
      if(  clr_col > 73  ) {
        clr_col = 43;
        clr_row = clr_row + 1;
        clr_for = 8;
        clr_bak = clr_bak + 1;
        clr_num = 30;
      };
  };
  GOTO IN_KEY;

FLASH:
  clr_col = 43;
  clr_row =  6;
  clr_bak =  0;
  clr_for = 128;

  while(  clr_row < 14  ) {
      clr_str = '  ';
      write(clr_str,clr_col,clr_row,clr_bak,clr_for);
      clr_num = clr_num +1;
      clr_for = clr_for +1;
      clr_col = clr_col + 4;
      if(  clr_col > 73  ) {
        clr_col = 43;
        clr_row = clr_row + 1;
        clr_for = 128;
        clr_bak = clr_bak + 1;
        clr_num = 30;
      };
  };
  GOTO IN_KEY;

FLASH_BOLD:
  clr_col = 43;
  clr_row =  6;
  clr_bak =  0;
  clr_for = 136;

  while(  clr_row < 14  ) {
      clr_str = '  ';
      write(clr_str,clr_col,clr_row,clr_bak,clr_for);
      clr_num = clr_num +1;
      clr_for = clr_for +1;
      clr_col = clr_col + 4;
      if(  clr_col > 73  ) {
        clr_col = 43;
        clr_row = clr_row + 1;
        clr_for = 136;
        clr_bak = clr_bak + 1;
        clr_num = 30;
      };
  };
  GOTO IN_KEY;

FINI:
  Kill_box;
end_of_mac:
};

/* ****************************************************************************** */
/*                                 MULTI-EDIT MACRO                               */
/*                                                                                */
/* Name: CGA                                   Timestamp: 06/04/88 02:46pm        */
/*                                                                                */
/* Description: Shows CGA color chart          Contributed by Dick Botbyl         */
/*                                                                                */
/* ****************************************************************************** */

macro cga dump {

  str clr_str, clr_bot, clr_key;
  int clr_bak, clr_num, clr_col, clr_row, clr_for;
  int flop, key_code;
                                         /*  Initialize variables  */

  clr_bot = 'ÆÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍ¹';
  clr_key = '³    [PgUp]    [PgDn]    [End]';

  flop = 0;

  put_box(43,4,78,24,lightgray,back_color,' Color Chart ',true);
  write(clr_bot,43,21,lightgray,back_color);
  write(clr_key,43,22,lightgray,back_color);
  GOTO NO_FLASH;

IN_KEY:
  Read_key;
    if(  Key1 == 0  ) {
      Key_code = Key2;
      if(  (Key_code == 79) | (key_code == 27)  ) {
        GOTO FINI;
      };
      if(  Key_code == 73  ) {
        flop = flop + 1;
      };
      if(  Key_code == 81  ) {
        flop = flop + 1;
      };
      if(  flop == 2  ) {
        flop = 0;
      };
      if(  flop == 0  ) {
        GOTO NO_FLASH;
      };
      if(  flop == 1  ) {
        GOTO FLASH;
      };
    };
    GOTO IN_KEY;

NO_FLASH:
  clr_col = 44;
  clr_row =  5;
  clr_num =  0;
  clr_bak =  0;
  clr_for =  0;
  while(  clr_num < 128  ) {
    clr_str = STR(clr_num);
      if(  clr_num < 10  ) {
        clr_str = '  ' + clr_str + ' ';
      };
      if(  clr_num > 9  ) {
        if(  clr_num > 99  ) {
          clr_str = clr_str + ' ';
				} else {
          clr_str = ' ' + clr_str + ' ';
        };
      };
    write(clr_str,clr_col,clr_row,clr_bak,clr_for);
    clr_num = clr_num +1;
    clr_for = clr_for +1;
    clr_col = clr_col + 4;
      if(  clr_col > 73  ) {
        clr_col = 44;
        clr_row = clr_row + 1;
      };
      if(  clr_col > 73  ) {
        if(  clr_for == 32  ) {
          clr_bak = clr_bak + 1;
          clr_for =  0;
        };
      };
  };
  GOTO IN_KEY;

FLASH:
  clr_col = 44;
  clr_row =  5;
  clr_bak =  0;
  while(  clr_num < 256  ) {
    if(  clr_num < 128  ) {
      clr_num = 128;
    };
    clr_str = STR(clr_num);
    clr_str = clr_str + ' ';
    write(clr_str,clr_col,clr_row,clr_bak,clr_for);
    clr_num = clr_num +1;
    clr_for = clr_for +1;
    clr_col = clr_col + 4;
      if(  clr_col > 73  ) {
        clr_col = 44;
        clr_row = clr_row + 1;
      };
      if(  clr_col > 73  ) {
        if(  clr_for == 64  ) {
          clr_bak = clr_bak + 1;
          clr_for = 32;
        };
      };
  };
  GOTO IN_KEY;


FINI:
  Kill_box;
end_of_mac:
};