macro_file genlist;

macro genlist
{
   int oldreg;
   real maxlines;

   push_undo;
   mark_pos;
   rm("userin^querybox/ML=3/T=GENLIST - Select Mode:/W=3/P=Mode:");
   if (return_int)
   {
      refresh=FALSE;
      working;
      eof;
      maxlines=real_i(c_line);
      tof;
      oldreg=reg_exp_stat;
      reg_exp_stat=TRUE;
      while (find_text(' +[0-9]+ +[0-9]+[-/.][0-9]+[-/.][0-9]+ +',0,_oldexp))
      {
         make_message("GENLIST Pass 1: Removing filedates and sizes...%"+rstr(100.0/maxlines*real_i(c_line),0,0));
          replace(" ");
         goto_col(1);
         while (find_text('  ',1,_oldexp))
         {
            replace(" ");
            home;
         };
      }
      eof;
      maxlines=real_i(c_line);
      tof;
      do
      {
         make_message("GENLIST Pass 2: Converting multiline descriptions...%"+rstr(100.0/maxlines*real_i(c_line),0,0));
         if (cur_char==' ')
         {
            if (c_line>1)
            {
               up; eol; right;
               run_macro("startup^dlword");
               while (((cur_char=="|") | (cur_char==" ")) and not (at_eol))
                     run_macro("mesys^delword");
            }
         }
         down;
         goto_col(1);
      }
      while (not (at_eof));
      eof;
      maxlines=real_i(c_line);
      tof;
      do
      {
         make_message("GENLIST Pass 3: Indenting...%"+rstr(100.0/maxlines*real_i(c_line),0,0));
         goto_col(1);
         while (at_eol) run_macro("startup^dlword");
         forward_till(" ");
         forward_till_not(" ");
         while (c_col<14) text(" ");
         down;
      }
      while (not(at_eof));
      goto_mark;
      make_message("GENLIST: Done.");
      reg_exp_stat=oldreg;
      refresh=TRUE;
      redraw;
      pop_undo;
   }
}