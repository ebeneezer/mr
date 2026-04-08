/*******************************************************************************
                            MULTI_EDIT MACRO FILE

Name: USERIN  <-  USERIN2

Description:  - - Included by USERIN.S - -
              Most of the general purpose user input routines.

SPECCHAR        - Changes untypeable character into the |xx convention.
VALCHAR         - Changes the |xx back to a character.
STRSRC          - Formats strings to be used in macro source code generators.
CHNGPARM        - Changes a single slash type parameter(/) in a global string.
GLOBALVARLIST   - Creates a menu of global pseudo-array elements.
DELETEITEM      - Shuffles global variable arrays.
CHECKEVENTS     - Mouse and Key event handler.
DB              - General purpose Database manager.
DB_MOVE_ITEM    - Moves records in a DB file.  For use by DB only!
DB_SEARCH       - Performs a search on a DB file.  For use by DB only!
EDITWINDOW      - File editing/examining macro.
EDITSPELL       - Invokes the spell checker while in EDITWINDOW.
EDITSEARCH      - Invokes search while in EDITWINDOW.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/


macro SPECCHAR TRANS2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

NAME:  SPECCHAR

DESCRIPTION:  This is a general purpose string manipulator that changes
certain "unprintable" characters to the Multi-Edit macro language ASCII
Character representation for the purpose of displaying them in prompts.  The
string is passed to this macro via the standard ME parameter passing
convention, and the result is returned in Return_Str.  This macro now simply
executes the function _SpecChar, which is in USERIN1.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  return_str = _SpecChar( mparm_Str );
}


macro VALCHAR TRANS2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

NAME:  VALCHAR

DESCRIPTION:  This is a general purpose string manipulator that changes
any occurance of the '|' character, indicating the presence of a numeric
representation of an ASCII character and converting it to that character.
The string is passed to this macro via the standard ME parameter passing
convention, and the result is returned in Return_Str.  This now simply calls
 _ValChar in userin1.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  return_str = _ValChar( mparm_str );
}

macro STRSRC TRANS2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

NAME:  STRSRC

DESCRIPTION:  This is a general purpose string manipulator that changes
any occurance of a "|" or a "'" character, which was entered by a user in a
prompt, so that the part of the setup that saves the settings by generating
macro source code will be able to generate string literals properly.
The string is passed to this macro via the standard ME parameter passing
convention, and the result is returned in Return_Str

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  str Tstr;
  int JX,Jy;
  TStr = MParm_Str;
  jx = 1;
CHECKQ:
  jx = XPOS('''',TStr,jx);
  if(  jx != 0  ) {
    TStr = Str_Ins('''',TStr,jx);
    jx = jx + 2;
    goto CHECKQ;
  }

  /* Create double bars */
  jx = 1;
CHECKQ2:
  jx = XPOS('||',TStr,jx);
  if(  jx != 0  ) {
    if(  (JX == Length(TStr)) |
       (XPOS(Copy(TStr,jx+1,1),'0123456789|',1) == 0)  ) {
      TStr = Str_Ins('||',TStr,jx);
    }
    jx = jx + 2;
    goto CHECKQ2;
  }
  Return_Str = Tstr;
  RM('SPECCHAR '+ Return_Str);
}

macro CHNGPARM TRANS2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

DESCRIPTION:
This macro is designed to change any "/X=" type parameter in any global string.
It should work for any parameter delimiters.  If the parameter does not exist
in the string, it will add it to the end of the string.

/G=           The global string name
Return_Str    The parameter syntax(Because this is a "/x=" type of parameter,
it cannot be a part of MParm_Str(it would be impossible to parse out unless we
used different parameter delimiters, which would be inconsistent)
/P=           The new parameter

Example:

  Set_Global_Str("TEST","/X=1/Y=2/Z=3");
  Return_Str = "/Y=";
  RM("CHNGPARM /G=TEST/P=10");

Global_Str("TEST") will now be: "/X=1/Y=10/Z=3"

As this macro is not particularly efficient, it should only be used in cases
of extremely long strings of parameters where only one parameter is being
changed and complete rebuilding of the string would take longer.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

  str g_name[20] = Parse_Str('/G=',MParm_Str),
      g_str[2048] = Global_Str(g_name);

  Return_Int = XPos(Return_Str,G_Str,1);
  if (Return_Int) {
    var_remove_str(return_str,g_str);
  } else {
    Return_Int = svl(g_str) + 1;
  }

  Set_Global_Str(g_name,
        str_ins(return_str + parse_str("/P=",Mparm_str),g_str,return_int));
}

macro GLOBALVARLIST TRANS2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

NAME:  GLOBALVARLIST

DESCRIPTION:  Creates a menu of global string "array elements".  This is
              assuming the use of globals as pseudo arrays by having the last
              part of the global name numeric characters and therefore can be
              referenced sequentially using a counter.
                Example:
                  Global_Str('TEST1');
                  Global_Str('TEST2');
                  Global_Str('TEST3');
              In the above example, "TEST" would be considered the base, and 1,
              2, and 3 would be the value of the index to reference each element.

PARAMETERS:
              /G= The base name of the globals
              /#= The amount of elements
              /SC=n the starting cursor pos (default = 1);

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

  int  old_win, jx, old_refresh, count, reverse ;
  str  gstr[20], event_str[20] ;

  old_win = window_id;
  old_refresh = refresh;
  refresh = FALSE;
  switch_window( window_count );
  create_window;
  window_attr = $80;
  count = parse_int('/#=', mparm_str);
  gstr = parse_str('/G=',mparm_str);
  reverse = parse_int('/REV=', mparm_str);
  if(  reverse  ) {
    jx = count;
  } else {
    jx = 1;
  }
  while(  c_line <= count  ) {
    put_line( global_str( gstr + str(jx) ) );
    if(  reverse  ) {
      --jx;
    } else {
      ++jx;
    }
    down;
  }
  tof;
  goto_line(parse_int('/SC=', mparm_str));
  event_str = str(global_int('MENU_LEVEL')) + 'GV';
  set_global_str(event_str + 'IPARM_1', '/DC=1/TP=15/L=0/C=1/MW=29/WIN=' + str(cur_window));
  RM( 'DATA_IN /T=HISTORY/#=1/A=2/PRE=' + event_str +
      '/X=' + parse_str('/X=', mparm_str) + '/Y=' + parse_str('/Y=', mparm_str) +
      '/H=' + parse_str('/H=', mparm_str)
    );
  refresh = false;
  return_str = get_line;
  delete_window;
  switch_win_id( old_win );
  refresh = old_refresh;
}

macro DELETEITEM TRANS2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

NAME:  DELETEITEM

DESCRIPTION: Performs a "shuffle" of global variables used as arrays to fill
in the gap caused by the deletion of a single element.

PARAMETERS:
            /G= The name of the "base" of the global variable.
            /#= The total amount of array elements.
            /T= The type of global 0=string 1=integer.
            Return_Int the starting point to begin shuffling.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  int  jx, count, type ;
  str  gstr[20] ;
  gstr = parse_str('/G=', mparm_str);
  count = parse_int('/#=', mparm_str);
  type = parse_int('/T=', mparm_str);
  jx = return_int;
  while(  jx <= count  ) {
    if(  type == 0  ) {
      set_global_str( gstr + str(jx), global_str( gstr + str(jx + 1)));
    } else {
      set_global_int( gstr + str(jx), global_int( gstr + str(jx + 1)));
    }
    ++jx;
  }
  return_int = count - 1;
}


macro CHECKEVENTS trans2  {
/*******************************************************************************
                                MULTI_EDIT MACRO

Name:  CheckEvents

Description:    Checks to see if a keyboard or mouse event occurred.


Parameters:  /G=str         Global string prefix for event variables.
             /#=n           Number of events
             /M=n           0 = Check keyboard
                            1 = Check mouse
                            2 = Redraw events
                              /F=1  Update function key labels
                            3 = Clear events
                            4 = Center events on line.  IF you use this
                                DO NOT put /X= and /Y= coordinates in your
                                event globals, as this function will take
                                care of that for you.

                                /X=n  x coordinate
                                /Y=n  y coordinate
                                /W=n  width of area to be centered in
                                /S=n  starting # - 1.
                                /UC=1 Use /C= and /L= instead of /X=,/Y=.
                                /TW=n total width of fields (optional).
                                        If this parm is used, then the
                                        fields must contain /W= parms.

RETURNS:
            FOR /M=0 or 1
              Return_Int = 0 IF NO EVENT FOUND
              ELSE Return_Int = EVENT #.
              RETURN_STR = The event string.

            FOR /M=4
              Return_Int = Total width the events took up.

Global String Format:
            /T=str    title
            /X=n      X coordinate
            /Y=n      Y coordinate
            /W=n      Total event width.
            /K1=n     Keycode 1
            /K2=n     Keycode 2
            /KC=str   Keycode name
            /R=n      Result code
            /ND=1     No display
            /FL=      Function key label.  Will only work if /K1= and /K2=
                      define a function key.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  str  event_str[80], tstr[100], tstr2[100] ;
  int button_down = 0, bc = button_color, bk = button_key_color,
          w, y, k1, k2, jj, jx, jy, jz, tint, Event_Count, check_mode, fkeyl ;

  event_count = Parse_Int('/#=', mparm_str);
  event_str = Parse_Str('/G=', mparm_str );
  jx = 0;

  check_mode = parse_int('/M=', mparm_str);
  if(  check_mode == 1  ) {
    RETURN_INT = 0;
    while(  (jx < Event_Count)  ) {
      ++jx;
      tstr = Global_Str( event_str + str(jx) );
      y = parse_int('/Y=', tstr);

      if(  (Mou_Last_Y == y)  ) {
        jy = Parse_Int('/X=', tstr);
        w = parse_int('/W=', tstr);
        if(  (Mou_Last_X >= jy) & (Mou_Last_X < (jy + w))  ) {
          button_down = (parse_int('/3D=', tstr) != 0);
          call Hi_Event;
          button_down = 0;
        hi_loop:
          Mou_Check_Status;
          if(  (mou_last_status & 1)  ) {
            if(  (Mou_Last_X < jy) | (Mou_Last_X > (jy + w)) |
              (Mou_Last_Y != y)  ) {
              Call Draw_Event;
              Goto Exit;
            }
            Goto Hi_Loop;
          }
          goto event_found;
        }
      }
    }
  } else if(  check_mode == 2  ) {
    fkeyl = parse_int('/F=', mparm_str);
    while(  (jx < Event_Count)  ) {
      ++jx;
      tstr = Global_Str( event_str + str(jx));
      call Draw_Event;
    }
  } else if(  check_mode == 0  ) {
    RETURN_INT = 0;
    while(  (jx < Event_Count)  ) {
      ++jx;
      tstr = Global_Str( event_str + str(jx));
      if(  (Parse_Int('/K1=', tstr) == key1) & (Parse_Int('/K2=', tstr) == key2)  ) {
        RETURN_INT = Parse_Int('/R=', tstr);
        call hi_event;
        goto event_found;
      }
    }
  } else if(  check_mode == 3  ) {
    while(  (jx < Event_Count)  ) {
      ++jx;
      Set_Global_Str( event_str + str(jx), '');
    }
  } else if(  check_mode == 4  ) {
    var_parse_int( '/S=', mparm_str, jx );
    var_parse_int( '/TW=', mparm_str, jy );
    if(  jy == 0  ) {
      jy = 1;
      while(  (jx < event_count)  ) {
        ++jx;
        tstr = Global_Str( event_str + str(jx));
        var_parse_int('/W=', tstr, jj );
        if(  jj == 0  ) {
          jj = Length(parse_str('/KC=', tstr)) + Length(parse_str('/T=', tstr) );
          tstr = tstr + '/W=' + str( jj );
          Set_Global_Str( event_str + str(jx), tstr);
        }
        jy = jy + jj + 2;
      }
      jy = jy - 1;
    }
    tint = parse_int('/Y=', mparm_str);
    return_int = jy;

    jx = parse_int('/X=', mparm_str);
   /* make_message( parse_str('/W=', mparm_str) + ' ' + str(jy) );
    read_key; */
    jy = jx + (((parse_int('/W=', mparm_str) / 2)) - (jy / 2));
    if(  jy < jx  ) {
      jy = jx;
    }
    var_parse_int( '/S=', mparm_str, jx );
    var_parse_int( '/UC=', mparm_str, jz );

    while(  (jx < event_count)  ) {
      ++jx;
      tstr = Global_Str( event_str + str(jx));
      var_parse_int('/W=', tstr, jj );
      if(  jj == 0  ) {
        jj = Length(parse_str('/KC=', tstr)) + Length(parse_str('/T=', tstr) );
        tstr = tstr + '/W=' + str( jj );
        Set_Global_Str( event_str + str(jx), tstr);
      }
      if(  jz  ) {
        tstr = tstr + '/C=' + str(jy)/*  + '/L=' + str(tint) */;
      } else {
        tstr = tstr + '/X=' + str(jy) + '/Y=' + str(tint);
      }
      Set_Global_Str( event_str + str(jx), tstr);
      jy = jy + jj + 2;
    }
  }
  goto exit;

draw_event:
  jy = Parse_Int('/X=', tstr);
  y = Parse_Int('/Y=', tstr);
  tstr2 = Parse_Str('/T=', tstr);
  jj = svl(tstr2);
  if(  jj != 0  ) {
    WRITE( tstr2, jy + button_down, y, 0, bc );
  }
  tstr2 = Parse_Str('/KC=', tstr);
  WRITE( tstr2, jy + jj + button_down, y, 0, bk );
  if(  parse_int('/3D=', tstr)  ) {
    if(  button_down  ) {
      Write( ' ', jy, y, 0, m_t_color );
      Draw_Char( 32, jy + 1, y + 1, m_t_color, jj + svl(tstr2) );
    } else {
      Write( '|220', jy + jj + svl(tstr2), y, 0, button_shadow_color );
      Draw_Char( 223, jy + 1, y + 1, button_shadow_color, jj + svl(tstr2) );
    }
  }
  if(  fkeyl  ) {
    k1 = parse_int('/K1=', tstr);
    if(  k1 == 0  ) {
      k2 = parse_int('/K2=', tstr);
      if(  (k2 > 58) & (k2 < 114)  ) {
        flabel(Parse_Str('/FL=', tstr), k2 - 58, -1);
      }
    }
  }
  ret;

hi_event:
  bc = m_h_color;
  bk = m_h_color;
  call draw_event;
  bc = button_color;
  bk = button_key_color;
  ret;

Event_Found:
  RETURN_INT = jx;
  RETURN_STR = tstr;

exit:
}


macro DB trans2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

Name: DB

Description:  A text database manager, capable of considerable versatility.
              Can be used as for such simple things as establishing a pop-up
              phone list, to much more sophisticated uses as managing the
              filename extension setup parameters.  This macro can actually
              be nested so that editing a field can bring up yet another
              database.

File Format:  The header is composed of one field definition per line as
              follows (except for /DBF, the format is the same as individual
              string fields in DATA_IN):

    /TP=field_type/T=field_name/L=line/C=col/W=field_width/ML=max_field_length/DBF=field_designator
    (any number of repeats of the above line)
    ****START****
    (data records)

              The /DBF field designator must be unique, and represents the
              identifier in each record for that field.

              See PHONE.DB for a sample note card file.

Parameters:
            /X=   X coordinate for the menu box.
            /Y=   Y coordinate for the menu box.
            /F=  The file name of the database file.  If no path is
                 specified, then:
                    A.  the ME_PATH is used if no USER_ID is in use
                    B.  the USER_ID directory is used.
            /CP=  Create Prompt.  If present, will be the prompt when user
                  creates a new record.

            /LD= Leading delimiter for each field.  MUST be the last parameter
                 on the command line.  Default is "".

            /LT=  List title  If present, /LT= will replace the file name

            /DT=  Data title  If present, /DT= will be used in the DATA_IN
                  title instead of the list title.

            /NOALPHA=  If 1, No alphabetic sorting will take place.

            /2TOP= If 1, causes DB to move the selected choice to the top of the
                   list.

            /LO= 1 : List only.  Select with <ENTER>.  No DATA_IN screen.
                 2 : Same as 1 but adds the Modify option.

            /NL= No list.  If 1, the DVMENU of records is bypassed.  Display
                /FV= record.
            /FV=  Field value first field of record to be displayed.  Only
                  used if /NL=1 or /C=1.  If /FV is null, the first record will
                  be displayed.

            /PROTECT1 - nn=  Set of first field values of records to protect against
                              deletion.
                example:  /PROTECT1=TEST1/PROTECT2=TEST2

            /C=  Immediately create a new record if /FV= is not found.  Can be
                 used only with /NL=.

            /HF=str  Specifies a header file.  This allows you to use
                a seperate file for your field definitions.
            /PRE=str Prefix for the ISTR, IPARM and IINT.  Use when nesting
                DB.

            /RR=1 no longer supported

            /GLO= Name of global string to store a found record.  Can be used
              with or without /RR=.

            /DS=str Display string global var name.
                  The display string global should be formatted as follows:
                    /field_name=length /field_name=length...
                  ("/" should be replaced by the /LD= delimit)

           /PR=  Print records.  Will, instead of displaying the menu,
                 send it to the currently defined printer device.

           /NC=  If 1, disables Copy record function.

           /NE=  If 1, disables Edit record function.

          /NI= If 1, disables the Create record function.

           /NDF=1   Do NOT delete file window when done.
           /NDH=1   Do NOT delete header window when done.
           /ENC=1   Exit if no records are found.
           /MACRO=str  Name of macro to be run both BEFORE and AFTER
              a record is modified.  The record will be in the global
              variable specified by /GLO=.  The following parameters
              will be passed to the macro:

                  /P=nn     nn = 0  Macro was run before modification.
                               = 1  Macro was run AFTER modification.

                  /GLO=str      name of the global variable containing the
                                record.

          /DPT=str  Name of the page to use for the data in multiple page
                      db files.
          /HPT=str  Name of the page to use for the header in multiple page
                      db files.

          /SRP=n    1 = Allow search.

          /S=       Starting choice.  This will be the hilited choice if <> 0
          /NSF=     Don't save the DB file even if a change is made.  Rarely
                    used.

          /ABT=str  Accept button title override.  Defaults to "OK".
          /CBT=str  Cancel button title override.  Defaults to "Cancel".

          /VA=1     Verify canceling a data_entry screen.

      The following two parameters provide a means to add extra action buttons
      (/TP=11) to the record list box.
          /EBC=     Extra_Button_Count - The number of extra button.
          /EBN=     Extra_Button_Name  - The base name of the extra button
                    global array.  For instance, if you have /EBC=3 and
                    you have /EBN=EBTN, it would be expected that you
                    have the following global variables defined:
                    Global_Str('EBTN1')
                    Global_Str('EBTN2')
                    Global_Str('EBTN3')
                    The contents of these globals should be exactly the same
                    as the "IPARM" globals used in DATA_IN.


Global Variables Returned:
            Global_Int( '@DB_FILE_CHANGED' )  will be TRUE if any changes
            were made to the file.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

  int  x, y, xd, yd,      /* Display coordinates */
          field_count, /* The # of fields in the database */
          tp,         /* current record type */
          No_List,
          buttons_to_side = FALSE,
          t_refresh,
          Create,
          List_Only,
          Search_result,
          record_count, /* The # of records in the database */
          jx, jy, jz,      /* Temp vars */
          need_rebuild, /* 1 = Need new list of records */
          new_box,
          end_field,    /* The line number of the last field in the header.
                         end_field will be 0 if a seperate header file is used */
          tbc,        /* The box count we started with */
          header_win, /* The database header window */
          old_win,    /* The window we started with */
          db_win,     /* The database data window */
          build_win,  /* The window to build the database menu in */
          db_win_num,
          build_win_num,
          max_width,  /* The length of the biggest record */
          use_ds,
          cur_item, cur_row,
          No_Alpha,
          old_width,
          first_display,
          Print_Records,
          Use_Ps,
          Handle,
          T_Insert_Mode,
          T_Truncate_Spaces,
          Ev_Count,
          header_page_line, data_page_line,
          db_exists, header_exists,
          temp_use_ds,
          old_backups,
          db_changed = FALSE,
          creating = 0,
          Cancel_Macro = Parse_Int('/CANMAC=',Mparm_Str),
          Delete_Macro = Parse_Int('/DELMAC=',Mparm_Str),
          Copying = 0,
          Extra_Button_Count = Parse_Int('/EBC=',MParm_Str),
          EBC
          ;
/* Used for parse_ds */
  int  tint, tint2 ;
/*
Def_Int(Ticks);
 */
  str  tstr[2000],
           tstr2[80],
           tstr3,
           header_file[80],
           Prefix[10],
           List_Title[78],
           Data_Title[78],
           mac_str,
           glo_str[16],
           PP_Str[10],
           DSG_Name[20],
           DSG_Ints[30],
           TP_STR,
           FV,
          Extra_Button_Name[20] = Parse_Str('/EBN=',MParm_Str)
          ;

  int  ds_count ;
  char Delimit;

  Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') + 1);

  Error_Level = 0;
  reg_exp_stat = TRUE;
  T_Insert_Mode = Insert_Mode;
  T_Truncate_Spaces = Truncate_Spaces;
  Truncate_Spaces = False;
  Old_Backups = Backups;
  Backups = FALSE;
  Use_Ps = 0;
  Return_Int = XPos('/LD=',MParm_Str,1);
  if(  (Return_Int)  ) {
    Delimit = Copy(MParm_Str,Return_Int + 4,1);
  } else {
    Delimit = '';
  }
  glo_str = Parse_Str('/GLO=',MParm_Str);
  No_Alpha = Parse_Int('/NOALPHA=',MParm_Str);
  No_List = Parse_Int('/NL=',MParm_Str);
  Create = Parse_Int('/C=',MParm_Str);
  Jx = XPos('/MACRO=',MParm_Str,1);
  if(  (Jx)  ) {
    mac_str = Copy(Mparm_str,Jx + 7,254);
    if(  (XPos(' ',Mac_Str,1) == 0)  ) {
      Mac_Str = Mac_Str + ' ';
    }
  }
  Print_Records = False;
  List_Title = Parse_Str('/LT=',MParm_Str);
  if(  (List_Title == '')  ) {
    List_Title = Truncate_Path(Parse_Str('/DPT=',MParm_Str));
    if(  (List_Title == '')  ) {
      List_Title = Truncate_Path(Parse_Str('/F=',MParm_Str));
    }
    if(  (Parse_Int('/TT=',MParm_Str) == 1)  ) {
      List_Title = Truncate_Extension(List_Title);
    }
  }

  Data_Title = Parse_Str('/DT=',MParm_Str);
  if(  (Data_Title == '')  ) {
    Data_Title = List_Title;
  }

  List_Only = Parse_Int('/LO=',MParm_Str);
  Search_Result = 1;
  t_refresh = refresh;

  refresh = false;
  old_win = window_id;
  build_win = 0;
  build_win_num = 0;
  Prefix = Parse_Str('/PRE=',MParm_Str);
  if(  prefix == ''  ) {
    prefix = str(global_int('MENU_LEVEL'));
  }

  field_count = 0;
  tbc = box_count;

  /* Calculate position of box */
  x = parse_int('/X=', mparm_str);
  y = parse_int('/Y=', mparm_str);
  Ds_Count = 0;
  Use_Ds = 0;
  new_box = TRUE;

  /* Get file name.  If no path is specified, then assume the ME directory */
  tstr = parse_str('/F=', mparm_str);

  if(  (Get_extension(Tstr) == '')  ) {
/* This provides support for a default DB file extension to be configured.  It
is overridden if the calling macro specifies an extension */
    if(  (Global_Str('@DB_EXTENSION') == '')  ) {
      Tstr = Tstr + '.DB';
    } else {
      Tstr = Tstr + '.' + Global_Str('@DB_EXTENSION');
    }
  }

  if(  get_path(tstr) == ''  ) {
    if(  user_id == ''  ) {
      tstr = CAPS(me_path + tstr);
    } else {
      tstr = CAPS(me_path + user_id + '.USR\' + tstr);
    }
    if(  switch_file( tstr ) == FALSE  ) {
      return_str = Truncate_Path(tstr);
      RM('MakeUserPath /DF=1');
      tstr = return_str;
    }
  }
  tstr = CAPS(fexpand(tstr));
/* code for default file extension was here before.  Looks like it should be
about 14 lines above here. */
  error_level = 0;
  db_exists = Switch_File(Tstr);
  if(  NOT(db_exists)  ) {
    create_window;
    load_file( tstr );
    window_attr = $80;
    if(  error_level != 0  ) {
      error_level = 0;
      file_name = tstr;
    }
    if(  parse_int('/NDF=', mparm_str) != 0  ) {
      db_exists = TRUE;
    }
  }
  window_attr = $81;
  db_win = window_id;
  db_win_num = cur_window;
  header_win = window_id;

  /* Check for a seperate header file */
  header_file = parse_str( '/HF=', mparm_str );
  if(  (Svl(header_file))  ) {
    if(  (Get_extension(Header_File) == '')  ) {
/* This provides support for a default DB file extension to be configured.  It
is overridden if the calling macro specifies an extension */
      if(  (Global_Str('@DB_EXTENSION') == '')  ) {
        Header_File = Header_File + '.DB';
      } else {
        Header_File = Header_File + '.' + Global_Str('@DB_EXTENSION');
      }
    }
    if(  get_path(header_file) == ''  ) {
      return_str = header_file;
      RM('MakeUserPath /DF=1');
      header_file = return_str;
    }
    header_file = CAPS(fexpand(header_file));
    /* If the header_file is not the same as the db file then load the header
      file */
    if(  header_file != tstr  ) {
      header_exists = Switch_File( header_file );
      if(  NOT(header_exists)  ) {
        create_window;
        load_file(header_file);
        window_attr = $80;
        if(  error_level != 0  ) {
          RM('MEERROR^Beeps /C=1');
          goto exit2;
        }
      }
      header_win = window_id;
    }
  }
  CALL find_page_lines;

/* Parse out the Display String array */
  if(  (Use_ds == false)  ) {
    tstr = global_str( parse_str('/DS=', mparm_str ) );
    use_ds = tstr != '';
    if(  use_ds  ) {
      Set_Global_Str('#DBDS@' + Str(Global_Int('MENU_LEVEL')),TStr);
      DSG_Name = '#DBDS';
      Call BUILD_DS;
      DS_Count = Jz;
    }
  }
  need_rebuild = true;
  call build_fields;

  if(  (Field_Count < 1)  ) {
    goto exit2;
  }
  tstr = global_str( 'DB#' + truncate_path(truncate_extension(file_name)) +
                              '^' + truncate_extension(parse_str('/DPT=', mparm_str)) );

/* If the calling macro specified a starting choice, use that instead. */
  cur_item = parse_int('/S=',MParm_str);
  if(  (Cur_Item < 1)  ) {
    cur_item = parse_int('/S=',tstr);
    cur_row = parse_int('/OR=', tstr );
  } else {
    cur_row = Cur_ITem;
  }
  if(  x == 0  ) {
    var_parse_int('/X=', tstr, x );
  }
  if(  y == 0  ) {
    var_parse_int('/Y=', tstr, y );
  }
  if(  y <= 0  ) {
    y = min_window_row + 1;
  }
  var_parse_int('/YD=', tstr, yd );
  var_parse_int('/XD=', tstr, xd );
  if(  yd == 0  ) {
    yd = y + 2;
  }

  if(  switch_win_id( db_win )  ) {
  }
  first_display = TRUE;


main_loop:
  /* Display the list of all of the fields */

  Copying = False;
  if(  (No_List)  ) {
    call NO_LIST_PROCESS;
    switch_win_id( db_win );
    call set_db_global;
    goto exit2;
  }
  call build_record_list;
  first_display = FALSE;
  if(  (Record_Count == 0)  ) {
    if(  ((Return_Int == 4) | ((Return_Int == 1) & (List_Only == 0)))  ) {
/* Don't allow editing if there is nothing to edit! */
      RM('MEERROR^BEEPS /C=1');
      Goto MAIN_LOOP;
    }
    if(  (Parse_Int('/ENC=',MParm_Str))  ) {
      Search_Result = 0;
      Goto EXIT;
    }
  }


  /* Exit from DB */
  if(  return_int == 0  ) {
    switch_win_id( db_win );
    call set_db_global;
    goto exit2;


  /* Modify was selected */
  } else if(  return_int == 4  ) {
    call modify_dialog;

  /* Display selected record */
  } else if(  return_int == 1  ) {
    if(  (List_Only)  ) {
      if(  parse_int( '/2TOP=', mparm_str ) & NOT(read_only)  ) {
        call move_item_to_top;
      }
      /* IF glo_str = '' THEN
        Goto NO_LIST_EXIT;
      END; */
    }
    if(  (List_Only > 0)  ) {
      call set_db_global;
      goto_line( cur_item + end_field );
      Set_Global_Str(glo_str,Get_Line);
    }
    Goto EXIT2;

  /* Copy a record */
  } else if(  return_int == 5  ) {
    call user_copy_record;

  /* Create new record.  Put it in the file in alphabetical order */
  } else if(  return_int == 12  ) {
    call user_insert_record;

/* Delete selected record */
  } else if(  return_int == 3  ) {
    call user_delete_record;
/* print the list */
  } else if(  return_int == 6  ) {
    Print_Records = True;
    call build_record_list;
/* search */
  } else if(  ((return_int == 20) | (return_int == 21))  ) {
    RM('DB_SEARCH /DW=' + Str(Db_Win) + '/FC=' + Str(Field_Count) + '/CI=' +
        Str(Cur_Item) + '/PRE=' + Prefix + '/R=' + Str(Return_Int == 21) +
        '/RC=' + Str(Record_Count) + '/EF=' + Str(End_Field));
    Cur_Item = Return_Int;
  } else if(  (Extra_Button_Count)  ) {
    Ebc = 0;
    while(  (Ebc < Extra_Button_Count)  ) {
      ++Ebc;
      if(  (Return_Int == Parse_Int('/R=',Global_Str(Extra_Button_Name + Str(Ebc) )))  ) {
        switch_win_id( db_win );
        call set_db_global;
        goto exit2;
      }
    }
  }

  goto main_loop;


user_insert_record:
  call check_read_only;
  if(  NOT(read_only)  ) {
    return_str = '';
    tstr3 = parse_str('/CP=', mparm_str);
    if(  tstr3 != ''  ) {
      RM('QUERYBOX /T=CREATE NEW RECORD/P=' + tstr3 + '/C=' + str(x + 2) +
            '/L=' + str( y + 2 ) + '/W=' + parse_str('/W=' ,global_str(Prefix + 'IPARM_1')) +
            '/ML=' + parse_str('/ML=' ,global_str(Prefix + 'IPARM_1')));
      if(  return_int < 1  ) {
        ret;
      }
    }
    Creating = True;
    call user_create_record;
  }
  ret;

modify_dialog:
    goto_line( end_field + cur_item );

modify_dialog2:
    Set_Global_Str( glo_str, get_line );
    if(  (Creating)  ) {
      TStr = Get_Line;
    }
    if(  mac_str != ''  ) {
      RM( mac_str + '/GLO=' + glo_str + '/CREATE=' + Str(Creating));
      if(  global_str( glo_str ) != get_line  ) {
        put_line( global_str( glo_str ) );
        db_changed = TRUE;
      }
    }

    call get_data;
    if(  (Creating)  ) {
      TStr = Get_Line;
    }

/* This stuff here will guarantee we will be able to compensate if lines in
a shared DB file are inserted or deleted while nested under DATA_IN */
    Mark_Pos;
    Set_Mark(1);
    if(  (Header_Win == Db_Win)  ) {
      Goto_Line(Header_Page_Line);
      Jy = C_Line;
      Mark_Pos;
    }
    Get_Mark(1);
    Jx = C_Line;
    RM('USERIN^DATA_IN /X=' + str(xd) + '/Y=' + str(yd) +
      '/NC=1/T=' + Data_Title +
      '/VA=' + parse_str('/VA=', mparm_str) +
      '/#=' + str(field_count) + '/PRE=' + Prefix + '/RGS=' +
      Parse_Str('/RGS=',MParm_Str) + '/H=' + Parse_Str('/H=',MParm_Str));
    xd = global_int('!DATA_IN_X');
    yd = global_int('!DATA_IN_Y');
/* Relocate the file position in case anything has changed */
   /*  call Find_Page_Lines;  */

/* If, while in DATA_IN, lines above this line were inserted or deleted, this
will compensate */
    if(  (Header_Win == DB_Win)  ) {
      Goto_Mark;
      Header_Page_Line = Header_Page_Line + (c_line - Jy);
    }
    Goto_Mark;
    End_Field = End_Field + (c_line - Jx);
    Data_Page_Line = Data_Page_Line + (c_line - Jx);

    call Set_Data_Page_Line;
    Set_Global_Str( glo_str, Get_Line);
    if(  return_int  ) {
      call set_data;
    } else if(  (Creating)  ) {
      if(  (Cancel_Macro)  ) {
        RM( mac_str + '/C=1/CREATE=1/GLO=' + glo_str);
      }
      Del_Line;
      Need_rebuild = True;
    } else if(  (Cancel_Macro)  ) {
/* If the post processing macro should be run even if editing was cancelled,
do it */
      RM( mac_str + '/C=1/GLO=' + glo_str);
      if(  (Return_Int)  ) {
        Put_Line(Global_Str(glo_str));
        Need_Rebuild = True;
      }
    }
    Copying = False;
    return_str = global_str(Prefix + 'ISTR_1');
    Creating = False;
  ret;

user_copy_record:
    call check_read_only;
    if(  NOT(read_only)  ) {

      if(  (switch_win_id( db_win ) )  ) {
        goto_line(cur_item + end_field);
        Return_Str = parse_str(Delimit + parse_str('/DBF=',global_str(Prefix + 'IPARM_1')) + '=',Get_Line);
      }
      RM('QUERYBOX /T=COPY A RECORD/P=' + Shorten_Str(parse_str('/T=' ,global_str(Prefix + 'IPARM_1'))) + ' /C=' + str(x + 2) +
            '/L=' + str( y + 2 ) + '/W=' + parse_str('/W=' ,global_str(Prefix + 'IPARM_1')) +
            '/ML=' + parse_str('/ML=' ,global_str(Prefix + 'IPARM_1')));
      if(  return_int > 0  ) {
        Copying = True;
        call copy_record;
        call modify_dialog2;
      }
    }
    RET;

check_read_only:
  if(  (read_only)  ) {
    RM('MEERROR^MessageBox /B=1/T="' + file_name + '" IS LOCKED!/M=DB File is locked, no modifications will be allowed.');
  }
  RET;

USER_CREATE_RECORD:
  tstr = Delimit + parse_str('/DBF=', global_str(Prefix + 'IPARM_1')) + '=' + return_str;
  jx = 1;
  while(  jx < field_count  ) {
    ++jx;
    tstr3 = parse_str('/DDEF=', global_str(Prefix + 'IPARM_' + str(jx)));
    if(  svl(tstr3) != 0  ) {
      tstr = tstr + Delimit + parse_str('/DBF=', global_str(Prefix + 'IPARM_' + str(jx))) + '=' + tstr3;
    }
  }
  call insert_record;
  call modify_dialog2;
  ret;


NO_LIST_PROCESS:
    Return_Str = Parse_Str('/FV=',MParm_Str);
    call find_data;
    if(  ((No_List == 1) & (Search_Result == 0))  ) {
      if(  (List_Only == true)  ) {
        Set_Global_Str(glo_str,Get_Line);
        ret;
      }
      if(  (Create == 0)  ) {
        if(  (Parse_Str('/FV=',MParm_Str) == '')  ) {
          call MODIFY_DIALOG;
        }
      } else {
        call user_create_record;
      }
    }
    else
      call modify_dialog;
    RET;


user_delete_record:
  if(  (Record_Count > 0)  ) {
  /* Check to see if this record is protected */
    if(  (XPos('/PROTECT',MParm_Str,1))  ) {
      if(  (switch_win_id( db_win ) )  ) {
        call check_read_only;
        if(  read_only  ) {
          ret;
        }
        goto_line(cur_item + end_field);
        Tstr = Caps(Parse_Str(Delimit + parse_str('/DBF=',global_str(Prefix + 'IPARM_1')) + '=',Get_Line));
        Jx = 1;
  CHECK_PROTECT:
        Tstr3 = Caps(Parse_Str('/PROTECT' + Str(Jx) + '=',MParm_Str));
        if(  (Tstr3 != '')  ) {
          if(  (Tstr == Tstr3)  ) {
            RM('MEERROR^MessageBox /B=1/T=/M=This record is protected against deletion!');
            ret;
          } else {
            ++Jx;
            Goto CHECK_PROTECT;
          }
        }
      }
    }
    RM('USERIN^VERIFY /T=Delete this record ?/H=' + Parse_Str('/H=',MParm_Str) +
            '/C=' + str(x + 2 ) + '/L=' + str(y + 2) );
    if(  return_int  ) {
      call delete_record;
    }
  }
  ret;

  int  tx ;
BUILD_DS:
  Jz = 0;
  jx = 0;
  DSG_Ints = '';
pd_loop:
  jx = xpos( delimit, tstr, jx + 1);
  if(  jx != 0  ) {
    ++Jz;
    jy = xpos('=', tstr, jx + 1 );
    tstr2 = copy( tstr, jx, jy - jx + 1 );
    Set_Global_Str( str(Jz) + DSG_Name, tstr2 );
    jx = jy;
    DSG_Ints = DSG_Ints + CHAR( parse_int( tstr2, tstr ) );
    /* Set_Global_Int( str(Jz) + DSG_Name, parse_int( tstr2, tstr )); */
    goto pd_loop;
  }
  RET;

/* Searches for the specified record, according to field 1 */
find_data:
  if(  switch_win_id( db_win )  ) {
  }
  goto_line(end_field);
  goto_col(1);
search_again:
  Search_Result = find_text(Delimit + parse_str('/DBF=', global_str(Prefix + 'IPARM_1')) + '=' + return_str, 0, 0);
  if(  (search_result != 0) & (caps(
      parse_str( Delimit + parse_str('/DBF=', global_str(Prefix + 'IPARM_1')) + '=', get_line ))
          != caps( return_str ))  ) {
    eol;
    Goto search_again;
  }
  if(  (Search_Result == 0)  ) {
    Goto_line(end_field + 1);
  }
  cur_item = c_line - end_field;
  ret;

/* Retrieves the data in the current record and puts it into a DATA_IN compatible
 globals */
get_data:
  jx = 0;
  tstr = get_line;
  while(  jx < field_count  ) {
    ++jx;
    TStr3 = global_str(Prefix + 'IPARM_' + str(jx));
    tp = parse_int('/TP=', Tstr3);
/* Assign to a special global if one exists for this field.  Originally created
for format line initialization. */
    if(  (XPos('/GSTR=',Tstr3,1))  ) {
      if(  ((parse_str(Delimit + parse_str('/DBF=', Tstr3) +
        '=',tstr ) != '') | (Parse_Int('/GSET=',Tstr3) > 0))  ) {
        Set_Global_Str( Parse_Str('/GSTR=',Tstr3),
          parse_str(Delimit + parse_str('/DBF=',TStr3) + '=',TStr));
      }
    }
    if(  ((tp == 0) | (tp == 2) | (Tp == 6) | ((tp == 8) & (Parse_Str('/ISTR=',TStr3) == '')))  ) {
        set_global_str(Prefix + 'ISTR_' + str(jx),
          parse_str(Delimit + parse_str('/DBF=',Tstr3) + '=',
          tstr ));
    } else if(  (tp == 1) | (tp == 3) | (tp == 4) | (tp == 5) | (tp == 7) | (tp == 9) | (tp == 13)  ) {
        set_global_int(Prefix + 'IINT_' + str(jx),
          parse_int(Delimit + parse_str('/DBF=',TStr3) + '=',
          tstr ));
    } else if(  (tp == 12)  ) {
      var_parse_int( '/DBV=', tstr3, jz );
      var_parse_int( delimit + parse_str('/DBF=', tstr3 ) + '=', tstr, jy );
      var_parse_int( '/MIN=', tstr3, tx );
      if(  (tx != 0)  ) {
        if(  jy < tx  ) {
          jy = tx;
        }
      }
      if(  (jz == jy)  ) {
        set_global_int(Prefix + 'IINT_' + str(jx), 1 );
      } else {
        set_global_int(Prefix + 'IINT_' + str(jx), 0 );
      }
    }
  }
  ret;

delete_record:
  if(  switch_win_id( db_win )  ) {
    call check_read_only;
    if(  read_only  ) {
      ret;
    }
    goto_line( cur_item + end_field );
    if(  (Delete_Macro)  ) {
      Set_Global_Str( glo_str, Get_Line);
      RM( mac_str + '/D=1/GLO=' + glo_str);
    }
    del_line;
    db_changed = TRUE;
    --record_count;
    if(  switch_win_id( build_win )  ) {
      del_line;
      call set_max_width;
    }
    switch_win_id( db_win );
    if(  AT_EOF  ) {
      --cur_item;
    }
    if(  record_count < (screen_length - 3)  ) {
      new_box = TRUE;
    }
  }
  ret;

copy_record:
  if(  (switch_win_id( db_win ) )  ) {
    call check_read_only;
    if(  read_only  ) {
      ret;
    }
    goto_line(cur_item + end_field);
    tstr = get_line;
/* Place the prompted for first field value in place of the original for the copy */
    Tstr3 = Delimit + parse_str('/DBF=',global_str(Prefix + 'IPARM_1')) + '=';
    Tstr = Tstr3 + Return_Str + Copy(Tstr,
            Length(parse_str(Tstr3,tstr )) + Svl(Tstr3) + 1,2048);
    call insert_record;
  }
  ret;

  /* Inserts the record contained in tstr into the file at the current position */
insert_record:
  if(  (switch_win_id( db_win ) )  ) {
    call check_read_only;
    if(  read_only  ) {
      ret;
    }
    Call ALPHABETIZE;
    if(  NOT(AT_EOF)  ) {
      Insert_Mode = True;
      if ((xpos("\f",get_line,1) == 1) || (get_line == "****START****")) {
        eol;
        cr;
      } else {  //fixed problem with inserting stuff in the wrong place T.R.
        goto_col(1);
        cr;
        up;
      }
    }
    ++record_count;
    call Put_Line_Here;
    if(  record_count < (screen_length - 3)  ) {
      new_box = TRUE;
    }
  }
  ret;

/* Takes the current record out of the DATA_IN globals and puts it in the file */
set_data:
  call check_read_only;
  if(  read_only  ) {
    ret;
  }
  jx = 0;
  tstr = '';
  while(  jx < field_count  ) {
    ++jx;
    TStr3 = global_str(Prefix + 'IPARM_' + str(jx));
    tp = parse_int('/TP=', Tstr3);

/* If we are supposed to use a special global instead of the ISTR, do that */
    if(  (XPos('/GSTR=',Tstr3,1))  ) {
      if(  (Global_Str( Parse_Str('/GSTR=',Tstr3)) != '')  ) {
        tstr = tstr + Delimit + parse_str('/DBF=',Tstr3) + '=' +
              Global_Str(Parse_Str('/GSTR=',Tstr3));
      }
      Goto BYPASS_SET;
    }
    switch (tp) {
      case 0 :
      case 2 :
      case 6 :
      case 8 :
        if(  global_str(Prefix + 'ISTR_' + str(jx)) != ''  ) {
          tstr = tstr + Delimit + parse_str('/DBF=', tstr3 ) + '=' +
                global_str(Prefix + 'ISTR_' + str(jx));
        }
        break;

      case 1 :
      case 3 :
      case 4 :
      case 5 :
      case 7 :
      case 9 :
      case 13 :
          if(  (tp == 3)  ) {
    /* This looks like a fix T.R. */
            if(  global_int( prefix + 'IINT_' + str(jx)) == 0  ) {
              set_global_int( prefix + 'IINT_' + str(jx), 1);
            }
          }
          if(  global_int(Prefix + 'IINT_' + str(jx)) != 0  ) {
            tstr = tstr + Delimit + parse_str('/DBF=', global_str(Prefix + 'IPARM_' + str(jx))) + '=' +
                  str(global_int(Prefix + 'IINT_' + str(jx)));
          }
          break;

      case 12 :
        if(  global_int(Prefix + 'IINT_' + str(jx)) != 0  ) {
          tstr = tstr + Delimit + parse_str('/DBF=', global_str(Prefix + 'IPARM_' + str(jx))) + '=' +
                parse_str('/DBV=', global_str(prefix + 'IPARM_' + str(jx)));
        }
    }
BYPASS_SET:
  }
  if(  mac_str != ''  ) {
    Set_Global_Str( glo_str, tstr );
    Set_Global_Int('@DB_NEED_REBUILD!',0);
    RM( mac_str + '/P=1/GLO=' + glo_str );
    if(  (Return_Int == -100)  ) {
/* This will allow the post processing macro to delete the current record, for
example if the user does not put the proper or neccesary data */
      Del_Line;
      Need_Rebuild = True;
      Ret;
    }
    tstr = global_str( glo_str );
    Need_Rebuild = Global_Int('@DB_NEED_REBUILD!');
  }
  if(  ((tstr != get_line) | (Copying == True))  ) {
    del_line;
    db_changed = TRUE;
    if(  switch_win_id( build_win )  ) {
      goto_line( cur_item );
      del_line;
    }
    switch_win_id( db_win );
    Call ALPHABETIZE;
    if(  not(at_eof)  ) {
      Insert_Mode = True;
      cr;
      up;
    }

put_line_here:
    call check_read_only;
    if(  read_only  ) {
      ret;
    }

    put_line(tstr);
    cur_item = c_line - end_field;
    tstr = Delimit + parse_str('/DBF=', global_str(Prefix + 'IPARM_1') ) + '=';
    DSG_Name = '#DBDS';
    call parse_ds;
    if(  switch_win_id( build_win )  ) {
      goto_line( cur_item );
      goto_col( 1 );  set_indent_level;
      Insert_Mode = True;
      cr;
      up;
      put_line( tstr3 );
      if(  svl( tstr3 ) > max_width  ) {
        max_width = svl( tstr3 );
        new_box = true;
      }
    }
    switch_win_id( db_win );

   /* need_rebuild := true; */
  }
  ret;

/* Takes the file header that defines the fields and builds DATA_IN compatible
 global parameter variables. */
build_fields:
  Set_Global_Str(Prefix + 'IHELP1','');
  Set_Global_Str(Prefix + 'IHELP2','');
  Set_Global_Str(Prefix + 'IHELP3','');
  tp_str = '';
  if(  switch_win_id( header_win )  ) {
    field_count = 0;
    Goto_Line( header_page_line + 1);
  floop:
    tstr = get_line;
    if(  (tstr == '****START****') | (At_Eof)  ) {
      end_field = c_line;
      goto floop_exit;
    }
    if(  str_char(tstr,1) == '|12'  ) {
      if(  svl(tstr) > 0  ) {
        end_field = c_line;
        goto floop_exit;
      }
    }

/* This is for the new stuff for printer string and display string being in the
 header */
    if(  (XPos('@',TStr,1) == 1)  ) {
      if(  (XPos('@DISPLAY_STRING=',TStr,1) == 1)  ) {
        if(  NOT( use_ds )  ) {
          Set_Global_Str('#DBDS@' + Str(Global_Int('MENU_LEVEL')),
                          Copy(TStr,17,2048));
          TStr = Copy(TStr,17,2048);
          DSG_Name = '#DBDS';
          Call BUILD_DS;
          Ds_Count = Jz;
          use_ds = true;
        }
      } else if(  (XPos('@PRINTER_STRING=',TStr,1) == 1)  ) {
        Set_Global_Str('#DBPS@' + Str(Global_Int('MENU_LEVEL')),
                        Copy(TStr,17,2048));
        use_Ps = true;
      } else if(  (XPos('@IHELP',TStr,1) == 1)  ) {
        Set_Global_Str(Prefix + Copy(Tstr,2,6),Copy(TStr,9,2048));
      }
      Goto other_data;
    }

    ++field_count;
    set_global_str(Prefix + 'IPARM_' + str(field_count), tstr);

    if(  ((Parse_Int('/TP=',Tstr) == 5) | (Parse_Int('/TP=',Tstr) == 11))  ) {
/* This is to accomodate the type 5 true/false. */
        set_global_str(Prefix + 'ISTR_' + str(field_count), Copy(tstr,XPos('/ISTR=',Tstr,1) + 6,255));
    } else {
        set_global_str(Prefix + 'ISTR_' + str(field_count), parse_str('/ISTR=', tstr));
    }

    set_global_int(Prefix + 'IINT_' + str(field_count), parse_int('/IINT=', tstr));
/* Save a special string which ties the /TP= with the /DBF= for use by PARSE_DS */
    tp_str = tp_str + Delimit + Parse_Str('/DBF=', tstr) + '=' + Parse_Str('/TP=',Tstr);
other_data:
    down;
    goto floop;

  floop_exit:
  }

set_data_page_line:
  if(  (db_win != header_win) | (data_page_line != 0)  ) {
    if(  (data_page_line == header_page_line) & (db_win == header_win)  ) {
      data_page_line = end_field;
    }
    end_field = data_page_line;
  }
  if(  (Field_Count < 1)  ) {
    RM('MEERROR^MessageBox /B=2/T=NO HEADER/M=Header in "' + file_name + '" or file not found!  Cannot display menu.');
  }
  switch_win_id( db_win );
  ret;

/* Builds the Variable Length Menu that lists all of the records by their
 first field */

  int  button_lines, go_type ;

build_record_list:

  refresh = FALSE;
  if(  (First_Display)  ) {
    FV = Parse_Str('/FV=',Mparm_str);
  }
  if(  (Print_Records)  ) {
/* Open the printer device/file */
    if(  use_ps  ) {
      temp_use_ds = use_ds;
      use_ds = true;
      tstr = Global_Str('#DBPS@' + Str(Global_Int('MENU_LEVEL')));
      Call BUILD_DS;
      DS_Count = Jz;
    }
    RM('MEUTIL3^OPEN_CLOSE_FILE /M=1/FN=' + Global_Str('PRINTER_DEVICE'));
    if(  (Error_Level)  ) {
      RM('MEERROR');
      RET;
    } else {
      Handle = Return_Int;
    }
    RM('PRINT_CODE_MENU /I=1');
    if (Return_Str != '') {
      Set_Global_Str("@PRINT_CODE",Parse_str("C=",Global_Str("@PRINT_CODE")));
      RM('PRINTSTR /G=@PRINT_CODE/S=1/H=' + Str(handle));
      if (Error_Level) {
        RM('Meerror');
        RET;
      }
    }
//    PP_Str = ' /S=1';
    switch_win_id( build_win );
    Jx = Cur_Window;
    Working;
    Make_Message('Printing list...');
    Goto print_it;
  }

  if(  build_win == 0  ) {
    switch_window(window_count);
    create_window;
    window_attr = $80;
    build_win = window_id;
    build_win_num = cur_window;
  }

  if(  switch_win_id( build_win )  ) {
    jx = cur_window;
    old_width = max_width;
    if(  (need_rebuild)  ) {
      working;
      need_rebuild = FALSE;
      erase_window;
      max_width = 31;
print_it:
      if(  switch_win_id( db_win )  ) {
        jy = cur_window;
        goto_col(1);
        goto_line(end_field + 1);
        tstr = Delimit + parse_str('/DBF=', global_str(Prefix + 'IPARM_1') ) + '=';

        record_count = 0;

/*    Ticks := MemP($46C);   */

        DSG_Name = '#DBDS';
        while(  not(at_eof) & (cur_char != '|12')  ) {

            ++record_count;

/* This will detect a "Skip_Over" situation */
          if(  (XPos(Delimit + '@|254=',Get_Line,1))  ) {
            TStr3 = Parse_Str(Delimit + '@|254=',Get_Line);
            if (!print_records) {
              TStr3 = TStr3 + '|254';
            }
            jz = svl(tstr3) - 1;
          } else {
            call parse_ds;
            jz = svl(tstr3);
          }

          if(  (Print_Records)  ) {
            Return_Str = TStr3 + '|13|10';

            RM('MEUTIL3^PRINTSTR /H=' + Str(Handle));
/*  PP_str is not needed now that we send the printer init string
            RM('MEUTIL3^PRINTSTR' + PP_Str + '/H=' + Str(Handle));
            PP_Str = ' ';
*/
            if(  (Error_Level)  ) {
              RM('MEERROR');
              RET;
            }
          } else {
            if(  (First_Display)  ) {
              if(  (FV != '')  ) {
                if(  (FV == Parse_Str(Tstr,Get_Line))  ) {
                  Cur_Item = Record_Count;
                }
              }
              }
            put_line_to_win( tstr3, record_count, build_win_num, false );
          }
          down;
          if (!(Print_Records)) {
            if(  jz > max_width  ) {
              max_width = jz;
            }
          }
        }

/* Make_Message('[' + Str(Memp($46C) - Ticks) + ']' + str(ticks)); */

        if(  (Print_Records)  ) {
      /* Close the printer device/file */
          RM('MEUTIL3^OPEN_CLOSE_FILE /H=' + Str(Handle));
          if(  (Error_Level)  ) {
            RM('MEERROR');
          }
          Print_Records = False;
          use_ds = TEMP_USE_DS;
          if(  use_ds  ) {
            tstr = Global_Str('#DBDS@' + Str(Global_Int('MENU_LEVEL')));
            Call BUILD_DS;
            DS_Count = Jz;
          }
          Need_Rebuild = TRUE;
          Make_Message('');
          RET;
        }
      }
    }
    if(  max_width < 31  ) {
      max_width = 31;
    }
    if(  max_width > (screen_width - 6)  ) {
      max_width = screen_width - 6;
    }
    if(  old_width != max_width  ) {
      new_box = true;
    }
    if(  cur_item > record_count  ) {
      cur_item = 1;
    }
    if(  new_box  ) {
      while(  (box_count > tbc)  ) {
        kill_box;
      }
    }
    switch_window(jx);
    if(  (Record_Count == 0)  ) {
      if(  (Parse_Int('/ENC=',MParm_Str))  ) {
        Ret;
      }
    }
    update_status_line;
   /* IF ((list_only > 0) and (list_only < 3)) THEN
      Set_Global_Str('DBIPARM_1', '/T=Select/K1=13/K2=28/R=1/L');
      Set_Global_Str('DBIPARM_2', '/T=Cancel/K1=27/K2=1/R=0/LL=1');
    ELSE
      Set_Global_Str('@DBEV1', '/T=Select/K1=13/K2=28/R=1/LL=1');
      Set_Global_Str('@DBEV2', '/T=Done/K1=27/K2=1/R=0/LL=1');
    END;
    */
/* This overrides events 1 or 2 */
 /*   IF (Parse_Str('/EV1=',MParm_Str) <> '') THEN
      Set_Global_Str('@DBEV1', Global_Str(Parse_Str('/EV1=',MParm_Str)));
    END;
    IF (Parse_Str('/EV2=',MParm_Str) <> '') THEN
      Set_Global_Str('@DBEV2', Global_Str(Parse_Str('/EV2=',MParm_Str)));
    END;
   */
    tstr = str(global_int('MENU_LEVEL')) + 'DB';
    Ev_Count = 0;
    jx = 1;
    jy = 1;
    jz = 0;
    if(  buttons_to_side  ) {
      ++Ev_Count;
      Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=11/T=Create/KC=<Ins>/W=11/K1=0/K2=82/R=12/L=1/C=' + str(max_width + 4));
      ++Ev_Count;
      Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=11/T=Delete/KC=<Del>/W=11/K1=0/K2=83/R=3/L=3/C=' + str(max_width + 4));
      ++Ev_Count;
      Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=11/T=Copy/KC=<F4>/W=8/K1=0/K2=62/R=5/FL=Copy/L=5/C=' + str(max_width + 4));
      ++Ev_Count;
      Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=11/T=Edit/KC=<F3>/W=8/K1=0/K2=61/R=4/FL=Edit/L=7/C=' + str(max_width + 4));
      jz = ev_count;
    } else {
      if(  (Parse_Int('/NI=',MParm_Str) == 0)  ) {
        ++Ev_Count;
        Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=11/T=Create/KC=<Ins>/W=11/K1=0/K2=82/R=12/L=1');
        jx = jx + 13;
      }
      if(  (Parse_Int('/ND=',MParm_Str) == 0)  ) {
        ++Ev_Count;
        Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=11/T=Delete/KC=<Del>/W=11/K1=0/K2=83/R=3/L=' + str(jy)/*  + '/C=' + str(jx) */);
        jx = jx + 13;
      }
      if(  (Parse_Int('/NC=',MParm_Str) == 0)  ) {
        if(  (jx + 6) > max_width  ) {
          call center_buttons;
          jx = 1;
          jy = jy + 2 ;
        }
        ++Ev_Count;
        Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=11/T=Copy/KC=<F4>/W=8/K1=0/K2=62/R=5/FL=Copy/L=' + str(jy)/*  + '/C=' + str(jx) */);
        jx = jx + 10;
      }
      if(  (Parse_Int('/NE=',MParm_Str) == 0)  ) {
        if(  (jx + 6) > max_width  ) {
          call center_buttons;
          jx = 1;
          jy = jy + 2;
        }
        ++Ev_Count;
        Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=11/T=Edit/KC=<F3>/W=8/K1=0/K2=61/R=4/FL=Edit/L=' + str(jy)/*  + '/C=' + str(jx) */);
        jx = jx + 10;
      }
      if(  (Parse_Int('/MOVE=',MParm_Str) != 0)  ) {
        if(  (jx + 6) > max_width  ) {
          call center_buttons;
          jx = 1;
          jy = jy + 2;
        }
        ++Ev_Count;
        Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=11/T=Move/KC=<F5>/W=8/K1=0/K2=63/R=50/FL=Move/L=' + str(jy)/*  + '/C=' + str(jx) */);
        jx = jx + 10;
      }
    }
    if(  (Parse_Int('/SRP=',MParm_Str) != 0)  ) {
      if(  (jx + 8) > max_width  ) {
        call center_buttons;
        jx = 1;
        jy = jy + 2;
      }
      ++Ev_Count;
      Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=11/T=Search/KC=<F6>/W=10/K1=0/K2=64/R=20/FL=Search/L=' + str(jy)/*  + '/C=' + str(jx) */);
      jx = jx + 12;
/* Put this back once we've got next search fixed */
      if(  (jx + 17) > max_width  ) {
        call center_buttons;
        jx = 1;
        jy = jy + 2;
      }
      ++Ev_Count;
      Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=11/W=19/T=Next Search/KC=<CtrlF6>/K1=0/K2=99/R=21/FL=Search/L=' + str(jy));
      jx = jx + 21;
    }
    if(  (Use_Ps)  ) {
      if(  (jx + 8) > max_width  ) {
        call center_buttons;
        jx = 1;
        jy = jy + 2;
      }
      ++Ev_Count;
      Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=11/T=Print/KC=<F8>/W=9/K1=0/K2=66/R=6/FL=Print/L=' + str(jy)/*  + '/C=' + str(jx) */);
      jx = jx + 11;
    }
    EBC = 0;
    while(  (EBc < Extra_Button_Count)  ) {
      int bw;
      ++EBC;
      bw = parse_int('/W=', Global_Str(extra_button_name + str(ebc)));
      if(  (jx + bw + 2) > max_width  ) {
        call center_buttons;
        jx = 1;
        jy += 2;
      }
      ++Ev_Count;
      Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count),'/TP=11' +
        Global_Str(Extra_Button_Name + Str(EBC)) + '/L=' + str(jy));
      jx += bw + 2;
    }
    if(  jx > 1  ) {
      call center_buttons;
      jy = jy + 2;
      jx = 1;
    }
    ++ev_count;
    go_type = 1;
    if(  list_only == 1  ) {
      go_type = 4;
    }
    Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=15/T=/C=1/L=' + str(jy) + '/WIN=' + str(cur_window) +
                          '/DC=1/W=' + str(max_width + 1) + '/OR=' + str( cur_row )/*  + '/GO=' + str(go_type) */);

    if(  (Parse_Int('/NB=',MParm_Str))  ) {
      New_Box = 0;
    }
    Goto_Line(Cur_Item);
    var_parse_str('/ABT=', mparm_str, tstr2);
    if(  svl(tstr2) == 0  ) {
      if(  ((list_only > 0) & (list_only < 3))  ) {
        Tstr2 = 'Go';
      } else {
        Tstr2 = 'Edit';
      }
    }
    var_parse_str('/CBT=', mparm_str, tstr3);
    if(  svl(tstr3) == 0  ) {
      Tstr3 = 'Done';
    }

    RM('USERIN^DATA_IN /A=3'/*  + str(list_only = 0) */ + '/X=' + str(x) + '/Y=' + str(y) +
        '/DBL=1/S=' + str(ev_count) +
        '/T=' + List_Title +
        '/H=' + Parse_Str('/H=',MParm_Str) +
        '/NB=' + str(new_box == 0) + '/OR=' + Str(Cur_Row) +
        '/PRE=' + tstr + '/#=' + Str(Ev_Count) +
        '/NK=1/ABT=' + tstr2 + '/CBT=' + Tstr3);

    if(  (return_int == 1) & (list_only == 0)  ) {
      return_int = 4;
    }
    x = global_int('!DATA_IN_X');
    y = global_int('!DATA_IN_Y');
    new_box = false;
    cur_item = c_line;
    cur_row = c_row;
    return_str = get_line;
    if (return_int == 50) {
      call db_move;
      goto build_record_list;
    }
    switch_win_id( db_win );
  }
  ret;



db_move:
  {
    int tpb = persistent_blocks;
    persistent_blocks = TRUE;

    new_box = TRUE;
    Set_Global_Str(tstr + 'IPARM_1', '/TP=10/T=User cursor keys to move item/C=1/L=1');
    ev_count = 2;
    Set_Global_Str(tstr + 'IPARM_' + Str(Ev_Count), '/TP=15/T=/C=1/L=' + str(jy) + '/WIN=' + str(cur_window) +
                          '/DC=1/W=' + str(max_width + 1) + '/OR=' + str( cur_row )
                          + '/DISPMAC=DB_MOVE_ITEM');

    Goto_Line(Cur_Item);
    set_global_int('!DB_CUR_ITEM', cur_item );
    block_begin;
    block_end;
    kill_box;
    RM('USERIN^DATA_IN /A=2/X=' + str(x) + '/Y=' + str(y) +
        '/DBL=1/S=' + str(ev_count) +
        '/T=' + List_Title +
        '/H=' + Parse_Str('/H=',MParm_Str) +
        '/NB=' + str(new_box == 0) + '/OR=' + Str(Cur_Row) +
        '/PRE=' + tstr + '/#=' + Str(Ev_Count) +
        '/NK=1/ABT=Ok/CBT=Cancel');
    jx = c_line;
    block_begin;
    block_end;
    if( return_int == 1) {
      switch_win_id( db_win );
      goto_line(cur_item + end_field);
      block_begin;
      block_end;
      goto_line( jx + end_field );
      if(cur_item < jx)
          down;
      move_block;
      cur_item = c_line - end_field;
      switch_win_id( build_win );
    }
    else {
      goto_line( cur_item );
      move_block;
    }
    block_off;
    persistent_blocks = tpb;
    ret;
  }

center_buttons:
  RM('CHECKEVENTS /M=4/G=' + tstr + 'IPARM_/#=' + str(ev_count) +
        '/S=' + str(jz) + '/UC=1/X=0/Y=' + str(jy) + '/W=' + str( max_width + 4 )
        + '/TW=' + str( jx - 2));
  jz = ev_count;
  ret;

/* This routine parses out the display line from the data line */
parse_ds:
  if(  NOT(use_ds)  ) {
    tstr3 = parse_str(tstr, get_line);
    Goto PARSE_DS_EXIT;
  }

  tstr3 = '';
  tstr = get_line;
  tint = 0;
  while(  tint < ds_count  ) {
    ++tint;
    tint2 = ASCII( str_char( dsg_ints, tint ) );

    if(  tint2 == 0  ) {
      if(  (Parse_Int(GLOBAL_STR(str( tint ) + DSG_NAME),tp_str) == 9)  ) {
/* This is to properly display keystroke fields */
        RM('SETUP^MAKEKEY /K1=' + Str(parse_int(GLOBAL_STR(str(tint) + DSG_NAME), tstr) & $FF) + '/K2=' +
          Str((parse_int(GLOBAL_STR(str(tint) + DSG_NAME), tstr) >> 8) & $FF));
        tstr3 = tstr3 + return_str + ' ';
      } else {
        tstr3 = tstr3 + parse_str( GLOBAL_STR(str( tint ) + DSG_NAME), tstr ) + ' ';
      }
    } else {
      if(  (Parse_Int(GLOBAL_STR(str( tint ) + DSG_NAME),tp_str) == 9)  ) {
/* This is to properly display keystroke fields */
        RM('SETUP^MAKEKEY /K1=' + Str(parse_int(GLOBAL_STR(str(tint) + DSG_NAME), tstr) & $FF) + '/K2=' +
          Str((parse_int(GLOBAL_STR(str(tint) + DSG_NAME), tstr) >> 8) & $FF));
        Tstr2 = COPY(Return_Str,1,tint2);
      } else {
        tstr2 = COPY(parse_str( GLOBAL_STR(str( tint ) + DSG_NAME), tstr ), 1, tint2 );
      }
      pad_str( tstr2, tint2, ' ' );
      tstr3 = tstr3 + tstr2;
    }
  }

PARSE_DS_EXIT:

  if(  (Tstr3 == '')  ) {
/* Expand the null string so it will work right with WMENU */
    Tstr3 = ' ';
  }
  ret;


/* Goes through the display file and resets the maximum width */
set_max_width:
  MARK_POS;
  tof;
  max_width = 30;
  while(  NOT( at_eof )  ) {
    tstr = get_line;
    jz = svl(tstr);
    if(  str_char( tstr, jz ) == '|254'  ) {
      --jz;
    }
    if(  jz > max_width  ) {
      max_width = jz;
    }
    down;
  }
  tof;
  ret;


move_item_to_top:
  if(  (Global_Int('@NO_DB_TO_TOP@'))  ) {
    Ret;
  }
  switch_win_id( db_win );
  Goto_Line(End_Field + Cur_Item);
  tstr = get_line;
  if(cur_item > 1 ) {
    del_line;
    goto_line( end_field);
    eol;
    Insert_Mode = true;
    cr;
    put_line( tstr );
    cur_item = 1;
  }
  ret;


find_page_lines:
  End_Field = 0;
  header_page_line = 0;
  data_page_line = 0;
  tstr = parse_str('/HPT=', mparm_str);
  if(  tstr != ''  ) {
    switch_win_id( header_win );
    tof;
    if(  find_text('%|12' + make_literal(tstr) + ' ||$',0,_OldExp)  ) {
      header_page_line = c_line;
      if(  find_text('%@*@*@*@*START@*@*@*@*$',0,_OldExp)  ) {
/* Look for end of header */
        End_Field = c_line;
      }
    }
  }

  tstr = parse_str('/DPT=', mparm_str);
  if(  tstr != ''  ) {
    switch_win_id( db_win );
    tof;
    if(  find_text('%|12' + make_literal(tstr) + ' ||$',0,_OldExp)  ) {
      data_page_line = c_line;
      if(  header_page_line == 0  ) {
        header_page_line = data_page_line;
        if( find_text('%@*@*@*@*START@*@*@*@*$', 0, _OldExp ) ) {
  /* Look for end of header */
          End_Field = c_line;
        }
      }
    } else if(  header_page_line != 0  ) {
      EOF;
      EOL;
      Insert_mode = true;
      CR;
      TEXT('|12'+ tstr );
      data_page_line = c_line;
    }
  }
  RET;

ALPHABETIZE:
  goto_col(1);
  if(  (No_Alpha)  ) {
    if(  (Record_Count < 1)  ) {
      Goto_Line(End_Field);
    } else {
      Goto_Line(End_Field + Cur_Item);
    }
  } else {
    goto_line(end_field+1);
    goto_col(1);
    tstr2 = Delimit + parse_str('/DBF=', global_str(Prefix + 'IPARM_1')) + '=';
    tstr3 = caps(parse_str(tstr2,tstr));
    while(  not(at_eof) & (caps(parse_str(tstr2,get_line)) < tstr3) &
      (Cur_Char != '|12')  ) {
      down;
    }
  }
  RET;
set_db_global:
  set_global_str( 'DB#' + truncate_path(truncate_extension(file_name))+
                  '^' +
                  truncate_extension(parse_str('/DPT=', mparm_str)),
                  '/S=' + Str(cur_item) + '/OR=' + str(cur_row) +
                  '/X=' + str(x) + '/Y=' + str(y) + '/XD=' + str(xd) +
                  '/YD=' + str(yd));
  ret;


exit:
  Return_Int = Search_Result - 1;
exit2:

  jx = 0;
  while(  jx < ds_count  ) {
    ++jx;
    SET_GLOBAL_INT( str( jx ) + '#DBDS', 0 );
  }

    Set_Global_Str('#DBPS@' + Str(Global_Int('MENU_LEVEL')),'');
    Set_Global_Str('#DBDS@' + Str(Global_Int('MENU_LEVEL')),'');

  Set_Global_Str(Prefix + 'IHELP1','');
  Set_Global_Str(Prefix + 'IHELP2','');
  Set_Global_Str(Prefix + 'IHELP3','');

  jx = 0;
  while(  jx < field_count  ) {
    ++jx;
    set_global_str(Prefix + 'IPARM_' + str(jx), '');
    set_global_str(Prefix + 'ISTR_' + str(jx), '');
    set_global_int(Prefix + 'IINT_' + str(jx), 0);
    Set_Global_Str(Prefix + 'DB_GSTR' + Str(jx),'');
  }
  if(  (parse_int('/NK=',MParm_str))  ) {
    ++tbc;
  }
  while(  box_count > tbc  ) {
    kill_box;
  }

  if(  switch_win_id( db_win )  ) {
    set_global_int('@DB_FILE_CHANGED', file_changed );
    if ((db_changed == true) && (Parse_Int('/NSF=',MParm_Str) == false)) {
      do {
        Error_Level = 0;
        save_file;
        if ( Error_Level ) {
          jx = menu_create;
          jy = return_int;
          menu_set_item(jx,1,' Retry ','','/L=2/QK=2/R=0/W=7/C=2',11,0,0);
          menu_set_item(jx,2,' Switch to DB window ','','/L=2/QK=2/R=1/W=21/C=11',11,0,0);
          menu_set_item(jx,3,' Abandon ','','/L=2/QK=2/R=3/W=10/C=35',11,0,0);
          menu_set_item(jx,4,'Switch to DB window will exit and bring the  ','','/L=5/C=1',10,0,0);
          menu_set_item(jx,5,'DB file up in a window for you to diagnose','','/L=6/C=1',10,0,0);
          menu_set_item(jx,6,'and remedy the problem manually.','','/L=7/C=1',10,0,0);
          menu_set_item(jx,7,'Abandon will cause you to loose all your','','/L=9/C=1',10,0,0);
          menu_set_item(jx,8,'changes.','','/L=10/C=1',10,0,0);
          RM('MEERROR^Beeps /C=2');
          Return_int = jx;
          RM('USERIN^DATA_IN /HN=1/#=8/T=ERROR ' + str(error_level) +
                  ' SAVING DB FILE/S=1');
          menu_delete(jx);
          jx = return_int;
          return_int = jy;
          switch ( jx ) {
            case 0 :
              break;
            case 1 :
              window_attr = 0;
              old_win = window_id;
              Goto SWITCH_DB;
              break;
            case 3 :
              Error_Level = 0;
              break;
          }
        }
      } while (Error_Level);
    }
    if(  NOT( db_exists )  ) {
      delete_window;
    }
  }

SWITCH_DB:
  if(  (db_win != header_win) & switch_win_id( header_win )  ) {
    if(  NOT( header_exists )  ) {
      delete_window;
    }
  }

  if(  switch_win_id( build_win )  ) {
    delete_window;
  }

  switch_win_id(old_win);
  Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') - 1);
  Set_Global_Str('@DBS_SEARCH','');
  Set_Global_Str('@DBS_FIELD','');
  Backups = Old_Backups;
  Insert_Mode = T_Insert_Mode;
  Truncate_Spaces = T_Truncate_Spaces;
  Refresh = T_Refresh;
  update_status_line;
}

macro db_move_item trans2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

Name: DB_MOVE_ITEM

Description:  Moves records in a DB file.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  int tpb = persistent_blocks;
  persistent_blocks = TRUE;
  refresh = false;
  int cl = global_int('!DB_CUR_ITEM');
  mark_pos;
  goto_line( cl );
  block_begin;
  block_end;
  goto_mark;
  if (cl < c_line)
    down;
  move_block;
  set_global_int('!DB_CUR_ITEM', c_line );
  refresh = true;
  redraw;
  persistent_blocks = tpb;
}

macro DB_SEARCH TRANS {
/*******************************************************************************
                                MULTI_EDIT MACRO

Name: DB_SEARCH

Description:  Performs a search in a db file.  For use by DB only.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  int Active_Window = Window_Id,
          Db_Window = Parse_int('/DW=',Mparm_Str),
          T_Refresh = Refresh,
          Field_Count = Parse_Int('/FC=',MParm_Str),
          Counter,
          T_Int,
          Type,
          Menu_Window = 0,
          Cur_Item = Parse_Int('/CI=',MParm_Str),
          T_Cur_Item = Cur_Item,
          Field_Width = 0,
          Max_Length = 0,
          T_Line,
          T_Col,
          T_Reg_Exp_Stat = Reg_Exp_Stat,
          T_Ignore_Case = Ignore_Case,
          Search_Amount,
          Data_Start,
          Field_Selection,
          Repeat = Parse_Int('/R=',MParm_Str),
          Repeat_Ok = False,
          Record_Count = Parse_Int('/RC=',Mparm_Str)
          ;
  str IParm[20] = Parse_Str('/PRE=',Mparm_Str) + 'IPARM_',
          Field_Title[80]
          ;
  Refresh = False;

  if (Repeat) {
/* Can't repeat if either global does not exist */
    if ((Length(Global_Str('@DBS_SEARCH')) == 0) |
        (Length(Global_Str('@DBS_FIELD')) == 0)) {
        Repeat = False;
    }
  }
  Reg_Exp_Stat = True;
  Ignore_Case = True;
  Create_Window;
  Menu_Window = Window_Id;
  Counter = 0;
  T_Int = 0;
  while (Counter < Field_Count) {
    ++Counter;
    Type = Parse_Int('/TP=',Global_Str(IParm + Str(Counter)));
    if ((Type == 0) | (Type == 3) | (Type == 8)) {
      Field_Title = Remove_Space(Parse_Str('/T=',Global_Str(IParm + Str(Counter))));

      if (Svl(Field_Title)) {
        T_Int = Parse_Int('/W=',Global_Str(IParm + Str(Counter)));
        if (T_Int > Field_Width) {
          Field_Width = T_Int;
        }
        T_Int = Parse_Int('/ML=',Global_Str(IParm + Str(Counter)));
        if (T_Int > Max_Length) {
          Max_Length = T_Int;
        }
/* This creates an array of DBF field values corresponding to each line */
        Set_Global_Str('@DBSDBF' + Str(C_Line),Parse_Str('/DBF=',Global_Str(IParm + Str(Counter))));
        if (Repeat) {
          if (Field_Title == Global_Str('@DBS_FIELD')) {
            Field_Selection = C_Line;
            Repeat_Ok = True;
          }
        }
        Put_Line(Field_Title);
        Down;
      }
    }
  }

  Tof;

  if ((Repeat == True) & (Repeat_Ok == True)) {
    Create_Global_Str('DBSISTR_1',Global_Str('@DBS_SEARCH'));
    Field_Title = Global_Str('@DBS_FIELD');
    Goto SKIP_PROMPT;
  }

  Create_Global_Str('DBSISTR_1','');
  Create_Global_Str('DBSIPARM_1','/T=Search For:/L=1/QK=1/C=1/W=' +
                    Str(Field_Width) + '/ML=' + Str(Max_Length) + '/GO=');
  Create_Global_Str('DBSIPARM_2','/TP=15/C=1/L=2/H=/QK=1/T=Field to search:/WIN=' +
                  Str(Cur_Window));
  RM('DATA_IN /PRE=DBS/#=2/T=SEARCH A FIELD');
  if (Return_Int == 0) {
    Goto EXIT;
  }
  Field_Title = Get_Line;
  Set_Global_Str('@DBS_SEARCH',Global_Str('DBSISTR_1'));
  Set_Global_Str('@DBS_FIELD',Field_Title);
  Field_Selection = C_Line;

SKIP_PROMPT:
  Switch_Win_Id(Db_Window);
  Working;
  RM('MEERROR^MESSAGEBOX /NW=1/T=DB SEARCH/M=Searching the file: "' + File_Name
      + '" for the string: "' + Global_Str('DBSISTR_1') + '" in the field: "' +
      Field_Title + '"');

/* I'm not using MARK_POS here for a very simple reason,  DB is using MARK_POS
to keep track when nesting DB.  Not using it here will help to prevent filling
up the bookmark stack */
  T_Line = C_Line;
  T_Col = C_Col;

  Data_Start = Parse_Int('/EF=',Mparm_Str) + 1;
  if (Repeat) {
    Goto_Line(Data_STart + Cur_Item);
  } else {
    Goto_Line(Data_Start);
    T_Cur_Item = 1;
  }

SEARCH_AGAIN:
  Search_Amount = Record_Count - T_Cur_Item + 1;

  if (Find_Text(make_literal(Global_Str('DBSISTR_1')),Search_Amount,_OldExp)) {
    Goto_Col(1);
    T_Cur_Item = C_Line - Data_Start + 1;
    if (Find_Text('{' + make_literal(Global_Str('@DBSDBF' + Str(Field_Selection))) +
                  '=[|0-}€-|255]*}{' + make_literal(Global_Str('DBSISTR_1')) +
                  '}{[|0-}€-|255]*||$}',1,_OldExp) == False) {

      Eol;
      Goto SEARCH_AGAIN;
    }
    Cur_Item = T_Cur_Item;
  } else if (Error_Level == 1015) {
    RM('MEERROR^MESSAGEBOX /B=2/M=Search aborted by user.');
  } else {
    RM('MEERROR^MESSAGEBOX /B=2/M=The string: "' + Global_Str('DBSISTR_1') +
        '" was not found in any "' + Field_Title + '" Field.');
  }
  Update_Status_Line;
  Kill_Box;
  Goto_Line(T_Line);
  Goto_Col(T_Col);

EXIT:
  Reg_Exp_Stat = T_Reg_Exp_Stat;
  Ignore_Case = T_Ignore_Case;
  Set_Global_Str('DBSISTR_1','');
  Set_Global_Str('DBSIPARM_1','');
  Set_Global_Str('DBSIPARM_2','');
  if(  (Switch_Win_Id(Menu_Window))  ) {
    Eof;
/* Get rid of DBF field array */
    Counter = C_Line;
    while (Counter) {
      Set_Global_Str('@DBSDBF' + Str(Counter),'');
      --Counter;
    }

    Delete_Window;
  }
  Switch_Win_Id(Active_Window);
  Refresh = T_Refresh;
  Return_Int = Cur_Item;

}

macro EDITWINDOW TRANS2 NO_BREAK {
/*******************************************************************************
                                MULTI_EDIT MACRO

Name: EditWindow

Description: Creates an editable window for inputing multiple lines of text.
      Uses the current window.  If you only want the text to be examined,
      not edited, then set the READ_ONLY switch to TRUE before calling
      EDITWINDOW.


Parameters:   /X=nn         X position
              /Y=nn         Y position
              /W=nn         Width
              /L=nn         Length
              /T=str        Title string.
              /A=n          0 = Done<F10>
                            1 = Ok<F10> Cancel<ESC>
              /WW=          1 = enable word wrap.
              /RM=          Right margin for wordwrap.  Defaults to 2048.
              /CC=          If 1, line changed color will be M_S_Color else
                            M_T_Color.
              /H=str        Help string.
              /SP=          1 = enable <AltS> to do a spell check.
              /SE=          1 = enable <F6> to do a search.
              /EV1=         Name of event global string to add.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

  int  jx, ev_count, x, y, w, l ,T_Mode,Extra_Event,
       old_refresh = refresh;
  str  event_str[20] ,Extra_Name[20];

  int accept = parse_int('/A=', mparm_str),
      menu = menu_create
      ;

  refresh = FALSE;
  Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') + 1);
  Extra_Event = false;
  Extra_Name = Parse_Str('/EV1=',MParm_Str);
  T_Mode = Mode;
  Mode = Edit;
  x = parse_int('/X=', mparm_str );
  y = parse_int('/Y=', mparm_str );
  w = parse_int('/W=', mparm_str );
  l = parse_int('/L=', mparm_str );
  if(  x == 0  ) {
    x = 10;
  }
  if(  y == 0  ) {
    y = 3;
  }
  if(  w == 0  ) {
    w = 40;
  }
  if(  l == 0  ) {
    l = 10;
  }
  if( w > (screen_width - 3) ) {
    w=screen_width - 3;
  }
  if( l > (screen_length - 7) ) {
    l=screen_length - 7;
  }

  call set_events;

  Right_Margin = Parse_Int('/RM=',MParm_Str);
  if(  ((Right_Margin < 1) | (Right_Margin > 2048))  ) {
    right_margin = 2048;
  }
  wrap_stat = Parse_Int('/WW=',MParm_Str);
  if( read_only ) {
    return_str = "View";
  }
  else
    return_str = "Edit";
  menu_set_item(menu, 1,
        return_str,"",
        '/QK=1/C=1/L=1/W=' + str(w) + '/HT=' + str(l) + '/WIN=' + str( cur_window ),
        16, 0, 0
        );
  return_int = menu;
  RM('USERIN^DATA_IN /HN=1/H=' + parse_str('/H=', mparm_str) + '/PRE=' + event_str +
        '/#=' + str(ev_count) + '/T=' + parse_str('/T=', mparm_str) + '/S=1' +
      '/X=' + str(x) + '/Y=' + str(y) +  '/A=' + str( accept == 0)
        );
  goto exit;

SET_EVENTS:
  ev_count = 1;
  if(accept) {
    menu_set_item( menu, ++ev_count,'OK', '', '/KC=<F10>/K2=68/R=1', 11, 0, 0);
    menu_set_item( menu, ++ev_count,'Cancel','','/KC=<ESC>/K1=27/K2=1/R=0', 11, 0, 0);
  } else {
    menu_set_item( menu, ++ev_count,'Done', '','/T=Done/KC=<F10>/K2=68/R=1', 11, 0, 0);
  }
  menu_set_item( menu, ++ev_count,'Help', '','/KC=<F1>/K2=59/R=2', 11, 0, 0);

  if(  (Parse_Int('/SP=',MParm_Str))  ) {
    menu_set_item( menu, ++ev_count,'Spell', 'EDITSPELL /WIN=' + str(cur_window),
        '/QK=1/R=10/M=1', 11, 0, 0);
  }
  if(  (Parse_Int('/SE=',MParm_Str))  ) {
    menu_set_item( menu, ++ev_count,'seaRch','EDITSEARCH /WIN=' + str(cur_window),'/QK=4/R=11/M=1', 11, 0, 0);
  }

  if(  (Extra_Name != '')  ) {
    Extra_Event = True;
/* Check to see if there is a macro to run and take appropriate action */
    Jx = Xpos("/MACRO=",Global_Str( Extra_Name ),1);
    if (Jx) {
      Jx += 7;
      Return_Str = "/M=1";
    } else
      Return_Str = "";
    menu_set_item( menu, ++ev_count,parse_str('/T=',Global_Str(Extra_Name)),
        Copy(Global_Str( Extra_Name ),jx,255),
        Global_Str( Extra_Name ) + Return_Str, 11, 0, 0 );
  }
  call center_buttons;
  RET;

center_buttons:
  {
    int tw, xw = 0, jx, x1 = 0;
    for ( jx = 2; jx <= ev_count; jx++ )
    {
      tw = length( parse_str( '/KC=', menu_item_str(menu,jx,3))) +
              length(menu_item_str( menu, jx, 1 ));
      menu_set_int( menu, jx, 3, tw );
      xw += tw + 2;
    }
    xw -= 2;
    x1 = (w / 2) - (xw / 2 ) + 1;
    if(x1 < 1)
      x1 = 1;
    for ( jx = 2; jx <= ev_count; jx++ )
    {
      menu_set_str( menu, jx, 3, menu_item_str( menu, jx, 3 ) + '/C=' + str(x1) + '/L=' + str(l + 3) );
      x1 += menu_item_int( menu, jx, 3 ) + 2;
    }
    ret;
  }

EXIT:
  Set_Global_Int('MENU_LEVEL', Global_Int('MENU_LEVEL') - 1);
  Mode = T_Mode;
  refresh = old_refresh;
  menu_delete( menu );
}

macro editspell trans2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

Name: EDITSPELL

Description:  Invokes the spell checker for EDITWINDOW

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  int  owin = cur_window
           ;
  refresh = false;
  switch_window( parse_int( '/WIN=', mparm_str ) );
  RM('SPELL /BC=' + str( box_count ) );
  refresh = false;
  switch_window( owin );
  return_int = 1;
}

macro editsearch trans2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

Name: EDITSEARCH

Description:  Invokes the search for EDITWINDOW

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  int  owin = cur_window
           ;
  refresh = false;
  switch_window( parse_int( '/WIN=', mparm_str ) );
  RM('SEARCH /BC=' + str(box_count) );
  refresh = false;
  switch_window( owin );
  return_int = 1;
}
