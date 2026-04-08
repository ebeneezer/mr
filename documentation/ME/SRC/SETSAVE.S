macro SETSAVE TRANS2 {
/*******************************************************************************
                               MULTI-EDIT MACRO

NAME:  SETSAVE

DESCRIPTION:  This macro creates and compiles INIT.S to save all setup
              parameters

PARAMETERS:
              /NKKW=1   Don't get rid of keymap window when done.  This
                        rarely used parameter was created specifically
                        for conversion macros so that the keymap window
                        can be retained and reused.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

#ifdef DEMO
	RM("MESSAGEBOX /B=2/T=OPERATION NOT AVAILABLE IN DEMO/M=The Multi-Edit Demo does not support permanent saving of configuration settings.");
#else
  str
          Whole_Line,
          F_Label[20],
          TStr,
          Temp_Stuff,
          Extension[80],
          Temp_Global,
          Program,
          e_f_name[128]
          ;

  int tid,Temp_Integer,Jx,Jy,T_Tab_Expand,T_Reg_Exp_Stat,Global_Count,
          P_Jx,P_Jy,T_Insert_Mode;
  int Active_Window,Keymap_Window,Macro_Window,Error_Window,
          Cmd, bc;
  str MStr,MName[30],KName[17],Mode_Str[10];

// Mod to handle correct placement of .TMP files
    RM("TMP_FILE_NAME /FN=SETER.TMP");
    e_f_name = return_str;

/* Initialize variables and save some system variables into temporary variables */
  if(  (Global_Str('@KEYMAP_NAME@') == '')  ) {
    Set_Global_Str('@KEYMAP_NAME@','KN=MULTI-EDITFN=KEYMAP');
  }
  T_Reg_Exp_Stat = Reg_Exp_Stat;
  T_Insert_Mode = Insert_Mode;
  Return_Str = '';
  Undo_Stat = False;
  T_Tab_Expand = Tab_Expand;
  Tab_Expand = False;
  Insert_Mode = True;
  Active_Window = Window_Id;
  Switch_Window(Window_Count);
  Create_Window;
  Macro_Window = Window_Id;
  Refresh = False;

  if(  (Global_Int('SETUP_CHANGED') & $02)  ) {

    Reg_Exp_Stat = False;
    RM('MEERROR^MessageBox /B=0/NW=1/T=SAVING CONFIGURATION/M=Building Keymap macro...');
  /*  Make_Message('Building Keymap macro...');  */
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
        RM('MEERROR^MESSAGEBOX /B=2/T=ERROR ' + Str(Error_Level) + '/M=Can''t find ' + Return_Str +
            '.  Install terminated.');
        Error_Level = 0;
        Goto END_OF_MAC;
      }
    } else {
      Tof;
      if(  (File_Changed)  ) {
        Error_Level = 0;
        Save_File;
        if(  (Error_Level)  ) {
          RM('MEERROR^MESSAGEBOX /B=2/T=ERROR ' + Str(Error_Level) + '/M=Can''t save ' + Return_Str +
              '.  Install terminated.');
          Error_Level = 0;
          Goto END_OF_MAC;
        }
      }
    }

    Keymap_Window = Window_Id;
    Switch_Win_Id(Macro_Window);
    Return_Str = User_Id + 'KEYMAP.S';
    RM('MakeUserPath');
    File_Name = Return_Str;
    PUT_LINE('macro ' + USER_ID + 'KEYMAP DUMP {');
    Insert_Mode = True;
    Down;
    Put_Line('// This macro was created by the Multi-Edit install macro version ' + Version );
    Down;
    Down;
    Put_Line('// Default key assignments');
    Down;
    Switch_Win_Id(Keymap_Window);

    if(  (Search_Fwd('****START****',0))  ) {
      Down;
      Goto_Col(1);
    }
    while(  (Not(At_Eof))  ) {
      TStr = Get_Line;
      Down;
      MName = Parse_Str('MC=',TStr);
      if(  (Mname != '')  ) {
        if(  (Switch_Win_Id(Macro_Window))  ) {
          mstr = Parse_Str('MID=', tstr );
          if(  mstr != ''  ) {
            set_global_str( mstr, '' );
          }
          KName = Parse_Str('K1=',Tstr);
          if(  (KName != '')  ) {
            Call Build_Key_Line;
            mstr = Parse_Str('MID=', tstr );
            if(  mstr != ''  ) {
              put_line('Set_Global_Str(''' + mstr + ''', ''' + kname + ''');');
              DOWN;
            }
          }
          KName = Parse_Str('K2=',Tstr);
          if(  (KName != '')  ) {
            Call Build_Key_Line;
          }

          Switch_Win_Id(Keymap_Window);
        }
      }
    }
    Switch_Win_Id(Macro_Window);
    Down;
    PUT_LINE('}');

    Save_File;
    Kill_Box;
/*
    Call COMPILE;
    if(  (Return_Int)  ) {
      Return_Str = 'COMP ERROR';
      Goto END_OF_MAC;
    } else {
*/
      if(  (Switch_Win_Id(Keymap_Window))  ) {
        if(  (Parse_Int('/NKKW=',MParm_Str) == 0)  ) {
          Delete_Window;
        }
      }
      Switch_Win_Id(Active_Window);
/*
    }
*/
  }

/* Create INIT.S */
  RM('MEERROR^MessageBox /B=0/NW=1/T=SAVING CONFIGURATION/M=Building "' + user_id + 'INIT.S"...');
  Switch_Win_Id(Macro_Window);
  Erase_Window;
  Undo_Stat = false;
  Return_Str = User_Id + 'INIT.S';
  RM('MakeUserPath');
  File_Name = Return_Str;
/* Create the start of a macro source code */
  PUT_LINE('macro ' + USER_ID + 'INIT DUMP {');
  Insert_Mode = True;
  Down;
  Put_Line('// This macro was created by the Multi-Edit install macro version ' + Version );
  Down;
  Down;
  Eol;
  Cr;
  Cr;
  Up;
  Up;

/* the Expanded memory settings need to come first */
  if(  global_int('!ADD_MEM_TYPE') != 0  ) {
    Put_Line('Init_Additional_Mem(' + Str(Global_Int('!ADD_MEM_AMOUNT')) + ', ' +
              Str(Global_Int('!ADD_MEM_TYPE')) + ');');
    Down;
    Put_Line('Set_Global_Int(''!ADD_MEM_AMOUNT'',' + Str(Global_Int('!ADD_MEM_AMOUNT')) + ');');
    Down;
    Put_Line('Set_Global_Int(''!ADD_MEM_TYPE'',' + Str(Global_Int('!ADD_MEM_TYPE')) + ');');
    Down;
  }
  Put_Line('Swap_Mode = ' + Str( swap_mode ) + ';');
  Down;
  Put_Line('Swap_Mem = ' + Str( Swap_Mem ) + ';');
  Down;

  /* Make_Message('Building "' + user_id + 'INIT.S"...'); */
  Working;


/* Create all the other settings, most are self explanitory */
  Down;
  Down;
  Put_Line('ERROR_LEVEL = 0;'); Down;
  RM('USERIN^STRSRC ' + Global_Str('@KEYMAP_NAME@'));
  Put_Line('Set_Global_Str(''@KEYMAP_NAME@'',''' + Return_Str + ''');');
  Down;
  Put_Line('RM(''' + User_Id + 'KEYMAP'');'); Down;
  Put_Line('if (ERROR_LEVEL != 0)'); Down;
  Put_Line('  RM(''KEYMAP'');'); Down;
  Put_Line('Set_Global_Str(''@ME_SERIAL#'',''' + Global_Str('@ME_SERIAL#') + ''');');
  Down;
  Put_Line('// Default settings');
  Down;
  Put_Line('Set_Global_Int(''Default_Insert_Mode'',' + Str(Global_Int('Default_Insert_Mode')) + ');');
  Down;
  Put_Line('Set_Global_Int(''REG_EXP_STYLE'',' + Str(Global_Int('REG_EXP_STYLE')) + ');');
  Down;
  Put_Line('Set_Global_Int(''SEARCH_INTR_STYLE'',' + Str(Global_Int('SEARCH_INTR_STYLE')) + ');');
  Down;
  Put_Line('Set_Global_Str(''SWITCHES'',''' + Global_Str('DEF_SWITCHES') + ''');');
  Down;
  Put_Line('Set_Global_Str(''DEF_SWITCHES'',''' + Global_Str('DEF_SWITCHES') + ''');');
  Down;
  Put_Line('Set_Global_Str(''REPL_SWITCHES'',''' + Global_Str('DEF_REPL_SWITCHES') + ''');');
  Down;
  Put_Line('Set_Global_Str(''DEF_REPL_SWITCHES'',''' + Global_Str('DEF_REPL_SWITCHES') + ''');');
  Down;
  Put_Line('Set_Global_Str(''FSWITCHES'',''' + Global_Str('DEF_FS_SWITCHES') + ''');');
  Down;
  Put_Line('Set_Global_Str(''DEF_FS_SWITCHES'',''' + Global_Str('DEF_FS_SWITCHES') + ''');');
  Down;
  Put_Line('Set_Global_Str(''FSEARCH_PATH'',''' + Global_Str('DEF_FS_PATH') + ''');');
  Down;
  Put_Line('Set_Global_Str(''DEF_FS_PATH'',''' + Global_Str('DEF_FS_PATH') + ''');');
  Down;
  Put_Line('Set_Global_Str(''FRSWITCHES'',''' + Global_Str('DEF_FSR_SWITCHES') + ''');');
  Down;
  Put_Line('Set_Global_Str(''DEF_FSR_SWITCHES'',''' + Global_Str('DEF_FSR_SWITCHES') + ''');');
  Down;
  Put_Line('Set_Global_Str(''FSEARCH_REPL_PATH'',''' + Global_Str('DEF_FSR_PATH') + ''');');
  Down;
  Put_Line('Set_Global_Str(''DEF_FSR_PATH'',''' + Global_Str('DEF_FSR_PATH') + ''');');
  Down;
  Put_Line('Set_Global_Str(''FC_CONFIG_STR'',''' + Global_Str('FC_CONFIG_STR') + ''');');
  Down;
  Put_Line('Set_Global_Int(''BINARY_MODE'',' + Str(Global_Int('BINARY_MODE')) + ');');
  Down;
  Put_Line('Insert_Mode = Global_Int(''Default_Insert_Mode'');');
  Down;
  Put_Line('Explosions = ' + Str(Explosions) + ';');
  Down;
  Put_Line('FKey_Delay = ' + Str(fkey_delay) + ';');
  Down;
  Put_Line('Backups = ' + Str(Backups) + ';');
  Down;
  Down;
	Put_Line('Set_Global_Str(''~BACKUP_PATH'', ''' + Global_Str('~BACKUP_PATH') + ''');');
	down;
	Put_Line('Set_Global_Str(''~TEMP_PATH'', ''' + Global_Str('~TEMP_PATH') + ''');');
	down;
	down;
/* see the macro STRSRC in this same macro file for an explanation */
  RM('USERIN^STRSRC ' + Page_Str );
  Put_Line('Page_Str = ''' + Return_Str + ''';');
  Down;
  Put_Line('Truncate_Spaces = ' + Str(Truncate_Spaces) + ';');
  Down;

  tstr = Global_Str('!AUTOSAVEPARMS');
  var_remove_str('/H1=', tstr );
  var_remove_str('/H2=', tstr );
  Put_Line('Set_Global_Str(''!AUTOSAVEPARMS'',''' + tstr + ''');');
  Down;
  Put_Line('Set_Global_Int(''@DIALOG_STYLE'',' + Str(Global_Int('@DIALOG_STYLE')) + ');');
  Down;
  if(  (GLOBAL_INT('@DIALOG_STYLE') == 1)  ) {   /* set globals for help system */
    Put_Line('Set_Global_Str(''!ACCEPT_LABEL'',''OK<ENTER>'');');
    Down;
    Put_Line('Set_Global_Str(''!ACCEPT_KEY'',''<ENTER>'');');
    Down;
    Put_Line('Set_Global_Str(''!NEXT_KEY'',''<TAB>'');');
    Down;
    Put_Line('Set_Global_Str(''!PREV_KEY'',''<ShftTAB>'');');
    Down;
    Put_Line('Set_Global_Str(''!HISTORY_KEY'',''<>'');');
    Down;
    Put_Line('Set_Global_Str(''!GROUP_KEY'',''<>'');');
    Down;
  } else {
    Put_Line('Set_Global_Str(''!ACCEPT_LABEL'',''  OK<F10>'');');
    Down;
    Put_Line('Set_Global_Str(''!ACCEPT_KEY'',''<F10>'');');
    Down;
    Put_Line('Set_Global_Str(''!NEXT_KEY'',''<>'');');
    Down;
    Put_Line('Set_Global_Str(''!PREV_KEY'',''<>'');');
    Down;
    Put_Line('Set_Global_Str(''!HISTORY_KEY'',''<F4>'');');
    Down;
    Put_Line('Set_Global_Str(''!GROUP_KEY'',''<|26>'');');
    Down;
  }
  Put_Line('Set_Global_Int(''DIR_IMMEDIATE'',' + Str(Global_Int('DIR_IMMEDIATE')) + ');');
  Down;
  Put_Line('Set_Global_Int(''AUTO_ARRANGE_ICONS'',' + Str(Global_Int('AUTO_ARRANGE_ICONS')) + ');');
  Down;
  Put_Line('Set_Global_Int(''NO_CONFIRM_OVERWRITE'',' + Str(Global_Int('NO_CONFIRM_OVERWRITE')) + ');');
  Down;
  Put_Line('Set_Global_Int(''@PGM_MENU_BYPASS'',' + Str(Global_Int('@PGM_MENU_BYPASS')) + ');');
  Down;
  Put_Line('Set_Global_Int(''@WILDCARD_MODE'',' + Str(Global_Int('@WILDCARD_MODE')) + ');');
  Down;
  Put_Line('Set_Global_Int(''CURSOR_EOB'',' + Str(Global_Int('CURSOR_EOB')) + ');');
  Down;

  Put_Line('Set_Global_Int(''EXEC_ENV_MODE'',' + Str(Global_Int('EXEC_ENV_MODE')) + ');');
  Down;

  Put_Line('persistent_Blocks = ' + Str(persistent_Blocks) + ';');
  Down;
  Put_Line('stream_block_mode = ' + Str(stream_block_mode) + ';');
  Down;
/* AN INTENTIONAL SYNTAX ERROR TO TEST ERROR HANDLING.
PUT_LINE('PERSTENT_BLOCKS = ' + STR(PERSISTENT_BLOCKS) + ';');
DOWN;
*/
  Put_Line('Ctrl_Help = ' + Str(Ctrl_Help) + ';');
  Down;
  Put_Line('Ctrl_Z = ' + Str(Ctrl_Z) + ';');
  Down;
  Put_Line('Eof_Cr = ' + Str(Eof_Cr) + ';');
  Down;
  Put_Line('File_Locking = ' + Str(File_Locking) + ';');
  Down;
  Put_Line('Display_Tabs = ' + Str(Display_Tabs) + ';');
  Down;
  Put_Line('Tab_Expand = ' + Str(T_Tab_Expand) + ';');
  Down;
  Put_Line('Mouse_H_Sense = ' + Str(Mouse_H_Sense) + ';');
  Down;
  Put_Line('Mouse_V_Sense = ' + Str(Mouse_V_Sense) + ';');
  Down;
  Put_Line('Set_Global_Int( ''~INIT_MOUSE'',' + str(parse_int('/M=', global_str('@MOUSEPARMS'))) + ');');
  Down;
  Put_Line('Mou_Disappear = ' + Str(Mou_Disappear) + ';');
  Down;
  Put_Line('Set_Global_Str(''@MOUSEPARMS'',''' + global_str('@MOUSEPARMS') + ''');');
  down;
  RM('USERIN^STRSRC ' + Word_Delimits );
  Put_Line('Word_Delimits = ''' + Return_Str + ''';');
  Down;
  Put_Line('Ins_Cursor = ' + Str(Ins_Cursor) + ';');
  Down;
  Put_Line('Ovr_Cursor = ' + Str(Ovr_Cursor) + ';');
  Down;
  Put_Line('Max_Undo = ' + Str(Max_Undo) + ';');
  Down;
  Put_Line('Column_Move_Style = ' + Str(Column_Move_Style) + ';');
  Down;
  Put_Line('Set_Global_Str(''FORMAT_CODE_DELIMIT'',''' + Global_Str('FORMAT_CODE_DELIMIT') + ''');');
  Down;
  Put_Line('Set_Global_Str(''PRINTER_DEVICE'',''' + Global_Str('PRINTER_DEVICE') + ''');');
  Down;
  Put_Line('Set_Global_Str(''PRINTER_TYPE'', ''' + Global_Str('PRINTER_TYPE') + ''');');
  Down;
  Put_Line('Print_Margin = ' + Str(Print_Margin) + ';');
  Down;

  Put_Line('Set_Global_Int(''RESTORE'',' + Str(Global_Int('RESTORE')) + ');');
  Down;
  Put_Line('Set_Global_Str(''@RESTORE_PATH'', ''' + Global_Str('@RESTORE_PATH') + ''');');
  Down;
 /* The following 16 lines are for the screen configuration for status lines
 function key lines, file name lines, etc. */
  Put_Line('Set_Global_Str(''@SCREEN_SETA'',''' + Global_STR('@SCREEN_SETA') + ''');');
  Down;
  Put_Line('Set_Global_Str(''@SCREEN_SETB'',''' + Global_STR('@SCREEN_SETB') + ''');');
  Down;
  Put_Line('Set_Global_Int(''DEF_SCRN_STYLE'',' + Str(Global_Int('DEF_SCRN_STYLE')) + ');');
  Down;
  Put_Line('Set_Global_Int(''CUR_SCRN'',' + Str(Global_Int('DEF_SCRN_STYLE')) + ');');
  Down;
  Put_Line('Set_Global_Int(''SPLIT_EDGE_MODE'',' + Str(Global_Int('SPLIT_EDGE_MODE')) + ');');
  Down;
  Put_Line('Set_Global_Int(''!AutoArrangErrSrc'',' + Str(Global_Int('!AutoArrangErrSrc')) + ');');
  Down;

  Put_Line('Init_Video_Mode = ' + Str(Video_Mode) + ';');
  Down;
  Put_Line('Ext_Video_Mode = ' + Str(Ext_Video_Mode) + ';');
  Down;

  Put_Line('Error_Color = ' + Str(Error_Color) + ';');
  Down;
  Put_Line('Shadow_Color = ' + Str(Shadow_Color) + ';');
  Down;
  Put_Line('Shadow_Char = ''||' + str(ascii(Shadow_Char)) + ''';');
  Down;
  Put_Line('W_T_Color = ' + Str(w_t_color) + ';');
  Down;
  Put_Line('W_H_Color = ' + Str(w_h_color) + ';');
  Down;
  Put_Line('W_B_Color = ' + Str(w_b_color) + ';');
  Down;
  Put_Line('W_C_Color = ' + Str(w_c_color) + ';');
  Down;
  Put_Line('W_L_Color = ' + Str(w_l_color) + ';');
  Down;
  Put_Line('W_LB_Color = ' + Str(w_lb_color) + ';');
  Down;
  Put_Line('W_EOF_Color = ' + Str(w_EOF_color) + ';');
  Down;
  Put_Line('W_S_Color = ' + Str(w_s_color) + ';');
  Down;
  Put_Line('M_T_Color = ' + Str(m_t_color) + ';');
  Down;
  Put_Line('M_S_Color = ' + Str(m_s_color) + ';');
  Down;
  Put_Line('M_K_Color = ' + Str(m_k_color) + ';');
  Down;
  Put_Line('M_B_Color = ' + Str(m_b_color) + ';');
  Down;
  Put_Line('M_H_Color = ' + Str(m_h_color) + ';');
  Down;
  Put_Line('CB_H_Color = ' + Str(cb_h_color) + ';');
  Down;
  Put_Line('CB_T_Color = ' + Str(cb_t_color) + ';');
  Down;
  Put_Line('CB_S_Color = ' + Str(cb_s_color) + ';');
  Down;
  Put_Line('Button_Color = ' + Str(button_color) + ';');
  Down;
  Put_Line('Button_Key_Color = ' + Str(button_key_color) + ';');
  Down;
  Put_Line('Button_Shadow_Color = ' + Str(button_shadow_color) + ';');
  Down;
  Put_Line('D_T_Color = ' + Str(d_t_color) + ';');
  Down;
  Put_Line('D_S_Color = ' + Str(d_s_color) + ';');
  Down;
  Put_Line('D_B_Color = ' + Str(d_b_color) + ';');
  Down;
  Put_Line('D_H_Color = ' + Str(d_h_color) + ';');
  Down;
  Put_Line('H_T_Color = ' + Str(h_t_color) + ';');
  Down;
  Put_Line('H_T1_Color = ' + Str(h_t1_color) + ';');
  Down;
  Put_Line('H_T2_Color = ' + Str(h_t2_color) + ';');
  Down;
  Put_Line('H_T3_Color = ' + Str(h_t3_color) + ';');
  Down;
  Put_Line('H_S_Color = ' + Str(h_s_color) + ';');
  Down;
  Put_Line('H_B_Color = ' + Str(h_b_color) + ';');
  Down;
  Put_Line('H_H_Color = ' + Str(h_h_color) + ';');
  Down;
  Put_Line('H_R_Color = ' + Str(h_r_color) + ';');
  Down;
  Put_Line('H_F_Color = ' + Str(h_f_color) + ';');
  Down;
  Put_Line('FKey_Color = ' + Str(fkey_color) + ';');
  Down;
  Put_Line('FNum_Color = ' + Str(fnum_color) + ';');
  Down;
  Put_Line('Stat1_Color = ' + Str(stat1_color) + ';');
  Down;
  Put_Line('Stat2_Color = ' + Str(stat2_color) + ';');
  Down;
  Put_Line('Message_Color = ' + Str(message_color) + ';');
  Down;
  Put_Line('Working_Color = ' + Str(working_color) + ';');
  Down;
  Put_Line('Background_Color = ' + Str(background_color) + ';');
  Down;
  Put_Line('Set_Global_Str(''@SYNTAX_COLORS'',''' + Global_Str('@SYNTAX_COLORS') + ''');');
  Down;
  Put_Line('Keyword_Highlighting = ' + Str(Keyword_Highlighting) + ';');
  Down;
  Put_Line('Keyword_Cline_Override = ' + Str(Keyword_Cline_Override) + ';');
  Down;

/* Dos shell stuff */
  Put_Line('Set_Global_Str(''DIR_SORT_STR'',''' + Global_Str('DIR_SORT_STR') + ''');');
  Down;
  Put_Line('Set_Global_Int(''@DIR_MODE@'',' + Str(Global_Int('@DIR_MODE@')) + ');');
  Down;
/* Condensed mode globals */
/*
  Put_Line('Set_Global_Int(''CONDENSE_MODE'',' + Str(Global_Int('CONDENSE_MODE')) + ');');
  Down;
  Put_Line('Set_Global_Int(''CUTOFF_COL'',' + Str(Global_Int('CUTOFF_COL')) + ');');
  Down;
  RM('USERIN^STRSRC ' + Global_Str('CONDENSE_SEARCH'));
  Put_Line('Set_Global_Str(''CONDENSE_SEARCH'',''' + Return_Str + ''');');
  Down;
  Put_Line('Set_Global_Str(''CONDENSE_SWITCHES'',''' + Global_Str('CONDENSE_SWITCHES') + ''');');
  Down;
 */
/* Calculator Parameters */
  Temp_Stuff = '/X=' + Parse_Str('/X=',Global_Str('Calc_Params')) +
    '/Y=' + Parse_Str('/Y=',Global_Str('Calc_Params')) +
    '/BASE=' + Parse_Str('/BASE=',Global_Str('Calc_Params'));
  Put_Line('Set_Global_Str(''CALC_PARAMS'',''' + Temp_Stuff + ''');');
  Down;

/* The following are in conjuction with the keyboard key repeat(typematic) speed
setup macro which uses the new interrupt procedure */
  Put_Line('Set_Global_Int(''KEYSPEED'',' + Str(Global_Int('KEYSPEED')) + ');');
  Down;
  Put_Line('Set_Global_Int(''KEYDELAY'',' + Str(Global_Int('KEYDELAY')) + ');');
  Down;
  Put_Line('Set_Global_Int(''NO_KEYSPEED'',' + str( global_int('NO_KEYSPEED') ) + ');');
  Down;
  Put_Line('Set_Global_Str(''@DEFAULT_EXT_LIST'',''' + Global_Str('@DEFAULT_EXT_LIST') + ''');');
/* Put ending line */
  Down;
  PUT_LINE('}');
  Down;
  PUT_LINE("#INCLUDE " + user_id + "KEYMAP");
  Tab_Expand = True;
  Save_File;

  Call COMPILE;
  if(  (Return_Int == 0)  ) {
/*    copy_file( me_path + 'MAC\' + user_id + 'KEYMAP.MAC',
                  me_path + 'MAC\' + user_id + 'INIT.MAC',
                  TRUE );
*/
   /* Make_Message( 'Multi-Edit configuration saved.'); */
    Set_Global_Int('SETUP_CHANGED',0);

/* This is to clear out all existing function key labels in edit mode. */
    Jx = 4;
    while(  (Jx)  ) {
      --Jx;
      Jy = 10;
      while(  (Jy)  ) {
        Flabel('',(Jx * 10) + Jy,0);
        --Jy;
      }
    }

    RM( user_id + 'INIT' );
/* Need to reload the keystroke macros in case the keymapping overwrote something */
    RM('KEYMAC_LOAD /NE=1');
    Error_Level = 0;
//    Del_File(user_id + 'SETER.TMP');
    Del_File(e_f_name);
    Error_Level = 0;

    // TMJ - 01-15-93 08:52am
    // This is to  allow for other macros to be executed after the configuration
    // is changed
    temp_stuff = global_str('@CONFIG_MACRO@');
    while(  temp_stuff != ''  ) {
      jx = xpos( ';', temp_stuff, 1 );
      if(  jx == 0  ) {
        jx = svl(temp_stuff) + 1;
      }
      Return_Int = 1;
      if(  jx > 1  ) {
        RM( copy( temp_stuff, 1, jx -1)  );
      }
      temp_stuff = str_del( temp_stuff, 1, jx );
   //   if(  return_int == 0  ) {
   //     goto end_of_mac;
   //   }
    }


		rm("SETSCRN");

  } else {
   /* Make_Message( 'Error occurred!! Configuration NOT saved.');  */
    Return_Str = 'COMP ERROR';
  }

  Goto END_OF_MAC;
/*********************************** SUBROUTINES *******************************/
  BUILD_KEY_LINE:
    Jx = Parse_Int('MODE=',Tstr);
    if(  ((Jx < 1) | (Jx > 4))  ) {
      Jx = 1;
    }
    Mode_Str = Remove_Space(Copy('EDIT     DOS_SHELLTERM     ALL',
      ((Jx - 1) * 9) + 1, 9));
    if (  parse_int('EDBOX=', tstr) )
    {
      mode_str += "|0x80";
    }
    Cmd = (Parse_Str('MF=',TStr) == 'NOT APPLICABLE');
    if(  (Cmd)  ) {
      MStr = 'CMD_TO_KEY(';
    } else {
      MStr = 'MACRO_TO_KEY(';
    }
    MStr = MStr + KName + ',';
    if(  (Cmd == False)  ) {
      MStr = MStr + '''';
      if(  Parse_Str('MF=',Tstr) != ''  ) {
        mstr = mstr + Parse_Str('MF=',Tstr) + '^';
      }
    }
    MStr = MStr + MName;
    if(  (Cmd == False)  ) {
      MStr = MStr + ' ' + Parse_Str('PARAM=',TSTr) + '''';
    }
    Jx = Parse_Int('',Tstr);
    if(  ((Jx < 1) | (Jx > 4))  ) {
      Jx = 1;
    }
    MStr = MStr + ',' + Mode_Str + ');';
    Put_Line(MStr);
    Down;
    if(  (Parse_Str('FKL=',TStr) != '')  ) {
  /* See if this key is a function key */

      MStr = Parse_Str('F',KName);;
      if(  (MStr != '')  ) {
        while(  (XPos(Copy(Mstr,Svl(MStr),1),'0123456789',1) == 0)  ) {
          MStr = Copy(MStr,1,Svl(MStr) - 1);
          if(  (Mstr == '')  ) {
            Goto NO_LABEL;
          }
        }
        if(  (MStr != '')  ) {
          if(  (Val(jx,Mstr) == 0)  ) {
            if( (jx < 13 ) && (jx > 0) ) {
              if( !Xpos( 'CTRLSHFT', caps(kname), 1 ) &&
                  !Xpos( 'ALTSHFT', caps(kname), 1 ) ) {
                if(  (XPos('SHFT',Caps(KName),1))  ) {
                  if( jx > 10 )
                      jx = (jx - 10) + 42;
                  else
                    Jx = Jx + 10;
                }
                else if(  (XPos('CTRL',Caps(KName),1))  ) {
                  if( jx > 10 )
                      jx = (jx - 10) + 44;
                  else
                    Jx = Jx + 20;
                }
                else if(  (XPos('ALT',Caps(KName),1))  ) {
                  if( jx > 10 )
                      jx = (jx - 10) + 46;
                  else
                    Jx = Jx + 30;
                }
                else {
                  if( jx > 10 )
                      jx = (jx - 10) + 40;
                }

                Put_Line('FLABEL(''' + Parse_Str('FKL=',TStr) + ''',' + Str(Jx) +
                  ',' + Mode_Str + ');');
                Down;
              }
            }
          }
        }
      }
    }
  NO_LABEL:
  RET;

COMPILE:
  Reg_Exp_Stat = True;
  Return_Int = 0;
  TStr = Truncate_Path(File_Name);
  Kill_Box;
  RM('MEERROR^MessageBox /B=0/NW=1/T=SAVING CONFIGURATION/M=Compiling "'+ TStr +'"...');

/* build a command line to compile the macro */
  Whole_Line = 'CMAC.EXE -P' + GetUserMacroPath() +' ' + File_Name;

/* If the Dos version is 3.0 or above, then add the ME_Path to the compiler
command line to insure the correct version of the macro compiler gets run. */
  if(  ((Dos_Version & $00FF) > 2)  ) {
/* If the command line would be too long, don't do it. */
    if(  ((Svl(Whole_Line) + Length(Me_Path)) < 100)  ) {
      Whole_Line = ME_PATH + Whole_Line;
    }
  }

//  Del_File(user_id + 'SETER.TMP');
  Del_File(e_f_name);
  Error_Level = 0;
  Return_str = whole_line;
//  RM('MEUTIL1^EXEC /CMDLN=1/SCREEN=3/SWAP=0/MEM=0/RED=' + user_id + 'SETER.TMP');
  RM('MEUTIL1^EXEC /CMDLN=1/SCREEN=3/SWAP=0/MEM=0/RED=' + e_f_name);
  kill_box;
  if(  (Error_Level != 0)  ) {
      RM('MEERROR^Beeps /C=1');
/* Error messages for unsuccessful compile */
    if(  (Error_Level == 3008)  ) {
  NO_MEMORY_ERROR:
      rm('MEERROR^MessageBox /T=INSUFFICIENT MEMORY TO COMPILE ' + TStr +
          '!/B=2/M=Please exit editor and compile ' + TStr + ' in the Multi-Edit directory.');
      Error_Level = 0;
      Return_Int = 1;
      RET;
    }
    RM( 'MEERROR' );
    Return_Int = 1;
    RET;
  }
  return_str = '';
  return_int = 0;
/*  if(  exit_code != 0  ) { */
    Switch_Window(Window_Count);
    Create_Window;
    Error_Window = Window_Id;
		error_level = 0;
//    Load_File(user_id + 'SETER.TMP');
    Load_File(e_f_name);

    if (Error_Level) {
        RM('MEERROR^Beeps /C=1');
        RM('MEERROR^MessageBox /T=ERROR # ' + str(error_level) +
          '/B=2/M=Error attempting to load the compiler output file: ' +
					e_f_name + '!');
        Error_Level = 0;
        Return_Int = 1;
        goto BAD_COMPILE;
    }
    Ignore_case = True;
    if(  (Search_Fwd('{MEMORY}||{RUNTIME}',0))  ) {
      Goto No_Memory_Error;
    }
    Tof;
    if(  (Search_Fwd('{Error}||{Aborted}',0))  ) {
      RM('MEERROR^Beeps /C=1');
      RM('MEERROR^MessageBox /T=ERROR IN COMPILING ' + TStr +
            '!/B=2/M=' + Copy(Get_Line,1,76));
      Refresh = True;
      Kill_Box;
      Kill_box;
      New_Screen;
      Messages = True;
      Undo_Stat = True;
      Return_Int = 1;
      Goto BAD_COMPILE;
    }
    TOF;
    if (Search_Fwd("V" + copy(Version,1,3),1) == 0) {
  /* we should only get here if somehow a different version of the macro compiler
  was run than the version of ME.  A distinct possiblilty if multiple versions
  of ME are on the system. */
      RM('MEERROR^MESSAGEBOX /B=2/T=WARNING:  MACRO COMPILER REPORTS WRONG VERSION' +
            '/M=Unpredictable results could occur.  Check DOS environment for possible incorrect search path.');
      /* Make_Message(''); */
      Kill_Box;
      Kill_box;
      New_Screen;
      Messages = True;
      Undo_Stat = True;
      Return_Str = 'COMP WARNING';
    }
BAD_COMPILE:
    if(  (Switch_Win_Id(Error_Window))  ) {
      Delete_Window;
    }
/*  } */
  if(  return_str != 'COMP WARNING'  ) {
//    Del_File(user_id + 'SETER.TMP');
    Del_File(e_f_name);
  }
  Switch_Win_Id(Macro_Window);
RET;
/*******************************************************************************/

END_OF_MAC:
  while(  (box_count > bc) & (box_count > 0)  ) {
    kill_box;
  }

  if(  (Switch_Win_Id(Macro_Window))  ) {
    Delete_Window;
  }
/* clean up altered system variables */
  Reg_Exp_Stat = T_Reg_Exp_Stat;
  Undo_Stat = True;
  Tab_Expand = T_Tab_Expand;
  Insert_Mode = T_Insert_Mode;
#endif // DEMO

}