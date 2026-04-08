macro SETKEYS TRANS2 {
/*******************************************************************************
                               MULTI-EDIT MACRO

Name:  SETKEYS

Description:  This is the key remapping menu.

Parameters:
              /X=   X coordinate for menu box
              /Y=   Y coordinate for menu box

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  str Title[36];
  int SlashPos;

  if(  (Global_Str('@KEYMAP_NAME@') == '')  ) {
    Set_Global_Str('@KEYMAP_NAME@','KN=MULTI-EDITFN=KEYMAP');
  }
  Call VALIDATE_KEYMAP;
  if (Return_Int == -1) {
    goto SELECT_KEYMAP;
  }

EDIT_KEYMAP:
  Title = Parse_Str('KN=',Global_Str('@KEYMAP_NAME@'));
  SlashPos = 1;
  while ( SlashPos = XPos('/', Title, SlashPos) ) {
    Title = Str_Ins('///', Title, ++SlashPos++);
    Slashpos += 2;
  }
  Set_Global_Str('@KEY_BTN1','/R=10/QK=1/W=17/T=Select new keymap');
  RM('USERIN^DB /MOVE=1/SRP=1/NDF=1/NSF=1/EBC=1/EBN=@KEY_BTN/F=' +
  Parse_Str('FN=',Global_Str('@KEYMAP_NAME@')) +
'/NOALPHA=1/CBT=Done/PRE=KEY/GLO=@KEYREC#/CANMAC=1/LT=EDITING THE ' +
  Title +
//  Parse_Str('KN=',Global_Str('@KEYMAP_NAME@')) +
' KEYMAP.  SELECT A MACRO////COMMAND/DT=KEY ASSIGNMENT/H=INKE/MACRO=KEY_CHECK');
  if(  Global_Int('@DB_FILE_CHANGED')  ) {
    Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $02);
  }
  if (return_int == 10) {
    str T_KEY[36];
    T_Key = Global_Str('@KEYMAP_NAME@');
SELECT_KEYMAP:
    RM('USERIN^DB /NDF=1/F=MECONFIG/DPT=KMAPLST.DB/NC=1/NOALPHA=1/LO=1/PRE=KLIST/DT=Ä/LT=SELECT A KEYMAP/GLO=@KEYMAP_NAME@/H=INKE/MACRO=KEYLIST_CHECK');

    if(  ((Return_Int == 1) & (T_Key != Global_Str('@KEYMAP_NAME@')))  ) {
      Call VALIDATE_KEYMAP;
      if (Return_Int == -1) {
        goto SELECT_KEYMAP;
      }
      Set_Global_Int('SETUP_CHANGED',Global_Int('SETUP_CHANGED') | $02);
    } else {
      Set_Global_Str('@KEYMAP_NAME@',T_Key);
    }
    if(  (Global_Str('@KEYMAP_NAME@') == '')  ) {
      Set_Global_Str('@KEYMAP_NAME@','KN=MULTI-EDITFN=KEYMAP');
    }
    goto EDIT_KEYMAP;
  }
  Goto EXIT;

VALIDATE_KEYMAP:
  Return_Int = 0;
  Return_Str = Parse_Str('FN=',Global_Str('@KEYMAP_NAME@'));
  if(  (Global_Str('@DB_EXTENSION') == '')  ) {
    Return_Str = Return_Str + '.DB';
  } else {
    Return_Str = Return_Str + '.' + Global_Str('@DB_EXTENSION');
  }
  RM('MakeUserPath');
  if(  ((File_Exists(Return_Str) == False) &
      (File_Exists(Me_Path + Parse_Str('FN=',Global_str('@KEYMAP_NAME@')) +
      '.' + Get_Extension(Return_Str)) == False))  ) {
    RM("MEERROR^MessageBox /T=ERROR/B=2/M=The keymap data file: " +
        Return_Str +
        " does not exist.  You must change the file name for the keymap type: "
        + Parse_Str('KN=',Global_Str('@KEYMAP_NAME@')) +
        ", or select a different keymap with an existing keymap data file.");
    Return_int = -1;
  }
  RET;

EXIT:
  return_int = 0;
}

macro KEYLIST_CHECK TRANS2 {
/*******************************************************************************
                               MULTI-EDIT MACRO

Name:  KEYLIST_CHECK

Description:  Checks to see if the keymap db file associated with a created or
edited keymap exists and prompts the user for the appropriate action.
To be used ONLY by SETKEYS!

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  int Choice;
  str t_str[80],L[80];

  Set_Global_str('@KEYMAP_NAME@',Caps(Global_str('@KEYMAP_NAME@')));
  if(  (Parse_Int('/P=',MParm_Str))  ) {
CHECK_AGAIN:
    Return_Str = CAPS(Truncate_Path(Truncate_Extension(Parse_Str('FN=',Global_str('@KEYMAP_NAME@')))));
    if(  (Return_Str == '')  ) {
      RM('MEERROR^MESSAGEBOX /B=2/M=You did not enter a legal keymap file name!');
      Goto ILLEGAL_FILE_NAME;
    }
    if(  (Global_Str('@DB_EXTENSION') == '')  ) {
      Return_Str = Return_Str + '.DB';
    } else {
      Return_Str = Return_Str + '.' + Global_Str('@DB_EXTENSION');
    }
    RM('MakeUserPath /DF=1');
    t_str = Return_str;

    if(  (File_Exists(T_str) == 0)  ) {
      L = T_Str + ' DOESN''T EXIST';

FILE_TROUBLE:
      RM('USERIN^XMENU /B=1/X=10/Y=10/L=' + L +
          '/M=Copy-another-keymap-file(INKEY)change-File-name()');

      Choice = Return_Int;
      if(  (Choice == 1)  ) {
        Mark_Pos;
        Del_Line;
        RM('USERIN^DB /NDF=1/F=MECONFIG/X=5/Y=3/DPT=KMAPLST.DB/NC=1/NE=1/ND=1/NI=1/NOALPHA=1/LO=1/PRE=KLIST2/DT=Ä/LT=SELECT KEYMAP TO COPY/GLO=@KEYMAP_CHECK@/H=INKE');
        if(  (Return_Int == 1)  ) {
          Return_Str = Get_Path(t_Str) +
            Parse_Str('FN=',Global_str('@KEYMAP_CHECK@')) + '.' +
            Get_Extension(T_Str);
/* Check to see if the keymap file they selected is in memory */
          Choice = Window_id;
          if (Switch_File(Return_Str)) {
/* If so, get rid of it so that it won't mess up the copying process */
            if (File_Changed) {
              Save_File;
            }
            Delete_Window;
            Switch_Win_Id(Choice);
          }

          Choice = Copy_File(Return_Str,T_Str, false);
          if(  (Choice)  ) {
            RM('MEERROR^ERRMSG');
            L = 'ERROR ' + Str(Choice + 3000) + ' COPYING KEYMAP FILE';
            Goto_Mark;
            Up;
            Eol;
            Insert_Mode = True;
            Cr;
            Goto FILE_TROUBLE;
          }
          Set_File_Attr(T_Str,0);
          Set_Global_str('@KEYMAP_NAME@',
            'KN=' + Parse_Str('KN=',Global_str('@KEYMAP_NAME@')) +
            'FN=' + Truncate_Path(Truncate_Extension(T_Str)));
        }
        Goto_Mark;
        Up;
        Eol;
        Insert_Mode = True;
        Cr;
        Goto CHECK_AGAIN;
      } else if(  (Choice == 2)  ) {
ILLEGAL_FILE_NAME:
        Create_Global_Str('!KEY_ISTR_1','');
        Create_Global_Str('!KEY_IPARM_1','/C=1/L=1/W=8/H=INKE/T=Enter new filename(no extension)');
        RM( 'USERIN^DATA_IN /PRE=!KEY_/#=1/T=/X=10/Y=10');
        if(  (Return_Int)  ) {
          Set_Global_str('@KEYMAP_NAME@',
            'KN=' + Parse_Str('KN=',Global_str('@KEYMAP_NAME@')) +
            'FN=' + Global_Str('!KEY_ISTR_1'));
        }
        Set_Global_Str('!KEY_ISTR_1','');
        Goto CHECK_AGAIN;
      } else {
        Return_Int = -100;
      }
    }
  }
}

macro KEY_CHECK TRANS2 {
/*******************************************************************************
                               MULTI-EDIT MACRO

Name:  KEY_CHECK

Description:  Checks for key assigment conflicts with currently edited key
record and alerts user.  Also, facilitates keymap changes on the fly.  To be
used ONLY by SETKEYS!

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  str S_Str,G_Str;
  int T_Ignore_Case,T_Reg_Exp_Stat,K_Line,Check_Mode,T_Mode,Conflict;

  G_Str = Global_Str(Parse_Str('/GLO=',MParm_Str));
  Check_Mode = Parse_Int('MODE=',G_Str);
  if(  (Check_Mode < 1)  ) {
    Check_Mode = 1;
  }

/* If editing was cancelled, simply reassign the keystrokes that were there
 before */
  if(  (Parse_Int('/C=',MParm_Str) != 0)  ) {
    Goto REASSIGN_KEYS;
  }

  if(  (Parse_Int('/P=',MParm_Str) != 0)  ) {
    T_Reg_Exp_Stat = Reg_Exp_Stat;
    T_Ignore_Case = Ignore_Case;
    Reg_Exp_Stat = True;
    Ignore_Case = True;

    Mark_Pos;
    K_Line = C_Line;
    S_Str = Parse_Str('K1=',G_Str);
    if(  (S_Str != '')  ) {
      Call CHECK_FOR_CONFLICT;
      if(  (Conflict)  ) {
        Goto DONE_CHECKING;
      }
    }
    S_Str = Parse_Str('K2=',G_Str);
    if(  (S_Str != '')  ) {
      Call CHECK_FOR_CONFLICT;
    }
/*
MC=MAINHELPDESCR=Main help screenK1=<F1>K2=<AltH>FKL=HelpMODE=1MF=USERIN
 */
DONE_CHECKING:
    Goto_Mark;
    Reg_Exp_Stat = T_Reg_Exp_Stat;
    Ignore_Case = T_Ignore_Case;

/* Begin "on the fly" key changes support */
/* Assign the current keys in this record */
REASSIGN_KEYS:
    --Check_Mode;
/* Assign primary key if present */
    Return_Str = Parse_Str('K1=',G_Str);
    if(  (Return_Str != '')  ) {
      RM('MAKE_SCAN_CODES');
      if(  (Return_Int > 0)  ) {
        S_Str = Parse_Str('MF=',G_Str);
        if(  (S_Str == 'NOT APPLICABLE')  ) {
          RM('ASSIGN_CMD /C=' + Parse_Str('MC=',G_Str) +
                     '/K=' + Str(Return_Int) +
                     '/M=' + Str(Check_Mode));
        } else {
          Macro_To_Key(Return_Int,S_Str + '^' + Parse_Str('MC=',G_Str) +
            ' ' + Parse_Str('PARAM=',G_Str),Check_Mode);
        }
/* If this one is a function key, assign the label also */
        if(  (Global_Int('!FKEY@'))  ) {
          FLabel(Parse_Str('FKL=',G_Str),Global_Int('!FKEY@'),Check_Mode);
        }
      }
    }
/* Assign secondary key if present */
    Return_Str = Parse_Str('K2=',G_Str);
    if(  (Return_Str != '')  ) {
      RM('MAKE_SCAN_CODES');
      if(  (Return_Int > 0)  ) {
        S_Str = Parse_Str('MF=',G_Str);
        if(  (S_Str == 'NOT APPLICABLE')  ) {
          RM('ASSIGN_CMD /C=' + Parse_Str('MC=',G_Str) +
                     '/K=' + Str(Return_Int) +
                     '/M=' + Str(Check_Mode));
        } else {
          Macro_To_Key(Return_Int,S_Str + '^' + Parse_Str('MC=',G_Str) +
            ' ' + Parse_Str('PARAM=',G_Str),Check_Mode);
        }
/* If this one is a function key, assign the label also */
        if(  (Global_Int('!FKEY@'))  ) {
          FLabel(Parse_Str('FKL=',G_Str),Global_Int('!FKEY@'),Check_Mode);
        }
      }
    }
/* Assign menu global if present */
    S_Str = Parse_Str('MID=',G_Str);
    if(  (S_Str != '')  ) {
      Set_Global_Str(S_Str,Parse_Str('K1=',G_Str));
    }
  } else {
/* Unnassign the current keys in this record */
    --Check_Mode;
/* Unnasign primary key if present */
    Return_Str = Parse_Str('K1=',G_Str);
    if(  (Return_Str != '')  ) {
      RM('MAKE_SCAN_CODES');
      if(  (Return_Int > 0)  ) {
        Unassign_Key(Return_Int,Check_Mode);
/* If this one is a function key, kill the label also */
        if(  (Global_Int('!FKEY@'))  ) {
          FLabel('',Global_Int('!FKEY@'),Check_Mode);
        }
      }
    }
/* Unnasign secondary key if present */
    Return_Str = Parse_Str('K2=',G_Str);
    if(  (Return_Str != '')  ) {
      RM('MAKE_SCAN_CODES');
      if(  (Return_Int > 0)  ) {
        Unassign_Key(Return_Int,Check_Mode);
/* If this one is a function key, kill the label also */
        if(  (Global_Int('!FKEY@'))  ) {
          FLabel('',Global_Int('!FKEY@'),Check_Mode);
        }
      }
    }
/* Unnasign menu global if present */
    S_Str = Parse_Str('MID=',G_Str);
    if(  (S_Str != '')  ) {
      Set_Global_Str(S_Str,'');
    }
/* End "on the fly" key changes support */

  }
  Goto EXIT;


CHECK_FOR_CONFLICT:
    Conflict = False;
    S_Str = 'K?=' + S_Str;
    Tof;
KEEP_LOOKING:
    if(  (Search_Fwd(S_Str,0))  ) {
      T_Mode = Parse_Int('MODE=',Get_Line);
      if(  (T_Mode < 1)  ) {
        T_Mode = 1;
      }
/* check the mode to see if it is the same.  If either mode is ALL, consider
 it the same. */
      if ((C_Line == K_Line) || ((T_Mode != Check_Mode) && (t_mode != 4)
          && (check_mode != 4))) {
        Eol;
        Goto KEEP_LOOKING;
      }
      Conflict = True;
      RM('MEERROR^MessageBox /T=WARNING/B=1/M=The key:' + Copy(S_Str,5,20) +
          ' is already assigned to:' + Parse_Str('MC=',Get_Line));
    }
  RET;

EXIT:
  Return_int = 0;
}

macro DEFINE_KEY TRANS2 {
/*******************************************************************************
                               MULTI-EDIT MACRO

Name:  DEFINE_KEY

Description:  This macro is used by SETKEYS as a prompt for a keystroke to
              define a key code

Parameters:
              /X=   X coordinate for menu box
              /Y=   Y coordinate for menu box
              /STR= Supplied by DATA_IN as the default choice

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

  int X1,Y1;
  X1 = Parse_Int('/X=',MParm_Str) + 18;
  Y1 = Parse_Int('/Y=',MParm_Str) + 6;
  RM('USERIN^XMENU /X=' + Str(X1) + '/Y=' + Str(Y1) +
    '/B=1/S=1/L=KEY ASSIGNMENT/M=Define key assignment(INKE)Erase key assignment()');
  if(  (Return_Int == 0)  ) {
    Return_Str = Parse_Str('/STR=',MParm_Str);
    Goto EXIT;
  }

  if(  (Return_Int == 1)  ) {
    Put_Box(X1,Y1,X1 + 25,Y1 + 3,0,M_B_Color,'DEFINING A KEY ASSIGNMENT',true);
    Write('Press the desired key.',X1 + 1,Y1 + 1,0,M_B_Color);
    Read_Key;
    RM('MAKEKEY /K1=' + Str(Key1) + '/K2=' + Str(key2));
    if(  (Length(Return_Str) == 1)  ) {
/* If the keycode is alphanumeric, need to add the <> */
      Return_Str = '<' + Return_Str + '>';
    }
    Kill_Box;
  } else {
    Return_Str = '';
  }

EXIT:
}

macro KEYCODE FROM ALL TRANS2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

Name:  KEYCODE

Description:  The macro prompts for a key press and then displays the name and
  the main and extended codes for the key and any key assignments.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  int Message_Count = 0,Inq_Int,T_Int,Mode_Int;
  str Title_Str[60],Inq_Str[26],Message_Str[16],Mode_Str[9];

  Make_Message('Press the desired key.');
  READ_KEY;
  RM('MAKEKEY /K1='+Str(key1)+'/K2='+Str(key2));
  Title_Str = Return_Str + ' Keycode = ' + Str(key1) + ', Extended code = ' +
               Str(key2);

  Mode_Int = 0;
  Mode_Str = 'EDIT';
  Call INQUIRE_KEY;

  Mode_Int = 1;
  Mode_Str = 'DOS SHELL';
  Call INQUIRE_KEY;

  Mode_Int = 3;
  Mode_Str = 'TERMINAL';
  Call INQUIRE_KEY;

  Put_Box(10,4,64,9 + Message_Count,0,M_B_Color,Title_Str,1);
  if(  (Message_Count < 1)  ) {
    Write('THIS KEY IS NOT ASSIGNED TO ANYTHING',18,6,0,M_S_Color);
  } else {
    Write('KEY ASSIGNMENTS',29,5,0,M_S_Color);
    Write('TYPE            NAME                      MODE',11,6,0,M_S_Color);
    Write('ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ',11,7,0,M_b_Color);
  }
  Write('PRESS ANY KEY',30,8 + Message_Count,0,M_B_Color);
  while(  (Message_Count)  ) {
    Write(Global_Str('@KEY_MESSAGE' + Str(Message_Count)),11,7 + Message_Count,
          0,M_T_Color);
/* Deallocate the globals as we go */
    Set_Global_Str('@KEY_MESSAGE' + Str(Message_Count),'');
    --Message_Count;
  }
  Make_Message(Title_Str);
  Read_Key;
  Kill_Box;

  Goto EXIT;

INQUIRE_KEY:
  Inq_Int = Inq_Key(Key1,Key2,Mode_Int,Inq_Str);
  if(  (Inq_Int)  ) {
    ++Message_Count;
    if(  (Inq_Int == 1)  ) {
/* Macro assignment */
/* Strip off parameter list if present */
      T_Int = XPos(' ',Inq_Str,1);
      if(  (T_Int)  ) {
        Inq_Str = Str_Del(Inq_Str,T_Int,25);
      }
      Message_Str = 'MACRO           ';
    } else if(  (Inq_Int == 2)  ) {
/* Keystroke macro assignment */
      Message_Str = 'KEYSTROKE MACRO ';
      if(  (XPos('@KM!#',Inq_Str,1) == 1)  ) {
        RM('GET_DB_RECORD /GLO=@KEY_REC/F=MECONFIG/DPT=KEYMAC.DB/NDF=1/DBF=K/FV='
             + Str((Key2 << 8) | Key1));
        if(  (Return_Int == 1)  ) {
          Inq_Str = Parse_Str('D=',Global_Str('@KEY_REC'));
          Set_Global_Str('@KEY_REC','');
        } else {
          Inq_Str = '(unnamed)';
        }
      } else {
        Inq_Str = '(unnamed)';
      }
    } else {
/* Getting the integer value of the command is about the best we can do, short of
creating a huge table of command names to correspond to the values. */
      Inq_Str = Str(Inq_Int & $FF);
      Message_Str = 'COMMAND         ';
    }
    Inq_Str = Inq_Str +  '                         ';
    Set_Global_Str('@KEY_MESSAGE' + Str(Message_Count),Message_Str + Inq_Str +
                    Mode_Str);
  }
  RET;

EXIT:
}

macro MAKEKEY FROM ALL TRANS2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

Name:  MAKEKEY

Description:  Takes two integer keycode parameters and returns the name of the
  keycode in RETURN_STR.  See KEYCODE (above) for example.

Parameters:
              /K1=  Primary scan code.
              /K2=  Extended scan code.

Returns:
              Return_Str contains the key name

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

    int  what1, what0 ;

    What0 = Parse_Int('/K1=', MParm_Str);
    What1 = Parse_Int('/K2=', MParm_Str);
    Return_Str = '';
    if(  (What0 == 0) & (What1 == 0)  ) {
      Goto exit;
    }
    Return_Str = 'Alt ';
    if(  What0 ==  0  ) {
      if(  (What1 >= 59) & (What1 <= 68)  ) {
        Return_Str = Str(What1 - 58);
        Return_Str = 'F'+Return_Str;
        Goto Exit2;
      }
      if(  (What1 >= 84) & (What1 <= 93)  ) {
        Return_Str = Str(What1 - 83);
        Return_Str = 'ShftF'+Return_Str;
        Goto Exit2;
      }
      if(  (What1 >= 94) & (What1 <= 103)  ) {
        Return_Str = Str(What1 - 93);
        Return_Str = 'CtrlF'+Return_Str;
        Goto Exit2;
      }
      if(  (What1 >= 104) & (What1 <= 113)  ) {
        Return_Str = Str(What1 - 103);
        Return_Str = 'AltF'+Return_Str;
        Goto Exit2;
      }
      Return_Str = '';

      Return_Str = Parse_Str('/'+Str(What1)+'=',
      '/129=Alt0/130=Alt-/131=Alt=/16=AltQ/17=AltW/18=AltE/19=AltR/20=AltT/21=AltY/40=Alt''/39=Alt;/22=AltU/23=AltI/24=AltO/25=AltP/30=AltA/31=AltS/32=AltD/33=AltF/34=AltG/57=AltSPACE');
      if(  Return_Str == ''  ) {
        Return_Str  =  Parse_Str('/'+Str(What1)+'=',
      '/35=AltH/36=AltJ/37=AltK/38=AltL/44=AltZ/45=AltX/46=AltC/47=AltV/48=AltB/49=AltN/50=AltM/75=LF/77=RT/72=UP/80=DN/71=HOME/79=END/15=ShftTAB/115=CtrlLF/116=CtrlRT/119=CtrlHOME/117=CtrlEND/132=CtrlPGUP/118=CtrlPGDN');
      }
      if(  Return_Str == ''  ) {
        Return_Str  =  Parse_Str('/'+Str(What1)+'=',
    '/120=Alt1/121=Alt2/122=Alt3/123=Alt4/124=Alt5/125=Alt6/126=Alt7/127=Alt8/128=Alt9/82=INS/83=DEL/133=F11/134=F12/135=ShftF11/136=ShftF12/137=CtrlF11/138=CtrlF12/246=Btn2/240=MsUP/241=MsDN/243=MsRT/242=MsLF/250=MEVENT');
      }
      if(  Return_Str == ''  ) {
        Return_Str  =  Parse_Str('/'+Str(What1)+'=',
  '/73=PGUP/81=PGDN/141=CtrlUP/145=CtrlDN/143=CtrlCNTR/1=AltESC/251=MEVENT2/252=MEVENT3/76=CNTR/41=Alt`/51=Alt</52=Alt>/53=Alt?/146=CtrlINS/147=CtrlDEL/144=CtrlGREY+/142=CtrlGREY-/139=AltF11/140=AltF12/244=Btn0/245=Btn1/150=CtrlGREY*/55=AltGREY*'
        );

      }
      if(  Return_Str == ''  ) {
        Return_Str  =  Parse_Str('/'+Str(What1)+'=',
        '/74=AltGREY-/78=AltGREY+/166=AltGREYENTER/148=CtrlTAB/165=AltTAB/14=AltBS/28=AltENTER/26=Alt[/27=Alt]/3=Ctrl@/238=ScrollLockOn/239=ScrollLockOff/43=Alt\/162=AltIns/163=AltDel/151=AltHOME'
        + '/159=AltEND/153=AltPgUp/161=AltPgDn/152=AltUP/160=AltDN/155=AltLF/157=AltRT'
          );

      }
      if(  Return_Str == ''  ) {
        Return_Str  =  Parse_Str('/'+Str(What1)+'=',
          '/220=ShftMEVENT/221=ShftMEVENT2/222=ShftMEVENT3/223=CtrlMEVENT/224=CtrlMEVENT2/225=CtrlMEVENT3/226=AltMEVENT/227=AltMEVENT2/228=AltMEVENT3'
        );

      }


      if(  (what1 == 164)  ) {
        return_str = 'AltGREY/';
      }
      if(  (what1 == 149)  ) {
        return_str = 'CtrlGREY/';
      }

      Goto Exit2;
    } else if(what0 == 253) {
      return_str = 'CtrlShft' + parse_str('|127' + str(what1) + '=',
'1=ESC3=24=35=46=57=68=79=810=911=012=-13==14=BS148=TAB16=Q17=W18=E19=R20=T21=Y22=U23=I24=O25=P26=[27=]28=ENTER30=A31=S32=D33=F34=G35=H36=J37=K38=L39=;40=''41=`43=\44=Z45=X46=C47=V48=B49=N50=M'  +
'51=,52=.53=/150=Grey*57=SPACE94=F195=F296=F397=F498=F599=F6100=F7101=F8102=F9103=F10119=HOME141=UP132=PGUP142=GREY-115=LF143=CENTER116=RT144=GREY+117=END145=DN118=PGDN146=INS147=DEL137=F11138=F12224=GreyENTER'
      );
      goto exit2;
    } else if(what0 == 254) {
      return_str = 'AltShft' + parse_str('|127' + str(what1) + '=',
'1=ESC120=1121=2122=3123=4124=5125=6126=7127=8128=9129=0130=-131==14=BS165=TAB16=Q17=W18=E19=R20=T21=Y22=U23=I24=O25=P26=[27=]28=ENTER30=A31=S32=D33=F34=G35=H36=J37=K38=L39=;40=''41=`43=\44=Z45=X46=C47=V' +
'48=B49=N50=M51=,52=.53=/55=Grey*57=SPACE104=F1105=F2106=F3107=F4108=F5109=F6110=F7111=F8112=F9113=F10151=HOME152=UP153=PGUP74=GREY-155=LF157=RT78=GREY+159=END160=DN161=PGDN162=INS163=DEL139=F11140=F12166=GreyENTER'
      );
      goto exit2;
    }
      Return_Str = '';
  Return_Str = Parse_Str('/'+Str(What0)+','+Str(What1)+'=',
'/13,28=ENTER/13,224=GreyENTER/13,50=CtrlM/10,28=CtrlENTER/10,36=CtrlJ/27,1=ESC/27,26=Ctrl[/8,14=BS/8,35=CtrlH'+
'/10,224=CtrlGreyENTER/9,15=TAB/9,23=CtrlI/55,8=7/55,71=ShftHOME/56,9=8/56,72=ShftUP/57,10=9/57,73=ShftPGUP');
      if(  Return_Str == ''  ) {
        Return_Str  =  Parse_Str('/'+Str(What0)+','+Str(What1)+'=',
'/45,12=-/45,74=GREY-/42,9=*/42,55=GREY*/52,5=4/52,75=ShftLF/53,6=5/53,76=ShftCNTR/54,7=6/54,77=ShftRT/43,13=+/43,78=GREY+/49,2=1/49,79=ShftEND/50,3=2/50,80=ShftDN/51,4=3/51,81=ShftPGDN/48,11=0/48,82=ShftINS/46,52=./46,83=ShftDEL/252,57=CtrlSPACE'
          );
      }

      if(  (what0 == 47) & (what1 == 224)  ) {
        return_str = 'GREY/';
      }

      if(  Return_Str == ''  ) {
        Return_Str  =  Parse_Str('/'+Str(What0)+'=',
'/127=CtrlBS');
      }
      if(  Return_Str == ''  ) {
if(  (What0 >= 1) & (What0 <= 31)  ) {
    Return_Str = 'Ctrl'+Char(What0 + 64);
} else {
        Return_Str = char(What0);
        goto exit;
    }
      }
Exit2:
    Return_Str = '<' + Return_Str + '>';
EXIT:
}

macro MAKE_SCAN_CODES FROM ALL TRANS2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

Name:  MAKE_SCAN_CODES

Description:  Takes a valid keycode (like <CtrlF2>) and returns the proper
              scan codes.  Essentially the opposite of MAKEKEY.

Parameters:
              Return_Str should contain the key name

Returns:
              If successful, Return_Str contains:
                /K1=  Primary scan code.
                /K2=  Extended scan code.
              Otherwise, Return_Str = ''.
              Also, Return_Int contains the combined scan codes
              in a 2 byte value.  Otherwise, Return_Int = 0.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

  int jx,Jy;
  str KName[30],TStr;

  KNAME = caps(Return_Str);
  Return_Str = '';
  Return_Int = 0;
  if(  (Kname == '')  ) {
    Goto exit;
  }
/* Strip <> if present */
  if(  (Svl(KName) > 2)  ) {
    if(  (Str_Char(KName,1) != '<')  ) {
      Goto EXIT;
    }
    if(  (Str_Char(KName,Svl(KName)) != '>')  ) {
      Goto EXIT;
    }
    Kname = Caps(Copy(Kname,2,Svl(KName) - 2));
  }
  // Process CtrlShft and AltShft names
  if( copy( kname, 1, 8 ) == 'CTRLSHFT') {
    Return_Str = '/K1=253/K2=' + Parse_Str('' + copy(kname, 9, 254) + '=',
'ESC=12=33=44=55=66=77=88=99=100=11-=12==13BS=14TAB=148Q=16W=17E=18R=19T=20Y=21U=22I=23O=24P=25[=26]=27ENTER=28A=30S=31D=32F=33G=34H=35J=36K=37L=38;=39''=40`=41\=43Z=44X=45C=46V=47B=48N=49M=50' +
',=51.=52/=53GREY*=150SPACE=57F1=94F2=95F3=96F4=97F5=98F6=99F7=100F8=101F9=102F10=103HOME=119UP=141PGUP=132GREY-=142LF=115CENTER=143RT=116GREY+=144END=117DN=145PGDN=118INS=146DEL=147F11=137F12=138GREYENTER=224' );
    goto Exit;
  }
  if( copy( kname, 1, 7 ) == 'ALTSHFT') {
    Return_Str = '/K1=254/K2=' + Parse_Str('' + copy(kname, 8, 254) + '=',
'ESC=11=1202=1213=1224=1235=1246=1257=1268=1279=1280=129-=130==131BS=14TAB=165Q=16W=17E=18R=19T=20Y=21U=22I=23O=24P=25[=26]=27ENTER=28A=30S=31D=32F=33G=34H=35J=36K=37L=38;=39''=40`=41\=43Z=44X=45C=46V=47' +
'N=49M=50,=51.=52/=53GREY*=55SPACE=5F1=104F2=105F3=106F4=107F5=108F6=109F7=110F8=111F9=112F10=113HOME=151UP=152PGUP=153GREY-=74LF=155RT=157GREY+=78END=159DN=160PGDN=161INS=162DEL=163F11=139F12=140GREYENTER=166B=48');
    goto Exit;
  }

/* If it is a function key... */
  if(  (Xpos('F',Kname,1))  ) {
    TStr = Kname;
    if(  (XPos('SHFT',TStr,1) == 1)  ) {
      Tstr = Copy(Tstr,5,20);
      Jx = 25;
    } else if(  (XPos('CTRL',tstr,1) == 1)  ) {
      Tstr = Copy(Tstr,5,20);
      Jx = 35;
    } else if(  (XPos('ALT',tstr,1) == 1)  ) {
      Tstr = Copy(Tstr,4,20);
      Jx = 45;
    } else {
      Jx = 0;
    }
    if(  (Str_Char(Tstr,1) != 'F')  ) {
      Goto NOT_FKEY;
    }
    Tstr = Copy(Tstr,2,20);
    if(  ((Tstr == '') | (Svl(Tstr) > 2))  ) {
      Goto NOT_FKEY;
    }
    if(  (Val(Jy,Tstr) | (Jy < 1) | (Jy > 10))  ) {
      Goto NOT_FKEY;
    }
    Return_Str = Str(Jy + 58 + Jx);
    if(  (Jx)  ) {
      Jx = Jx - 15;
    }
    Set_Global_Int('!FKEY@',Jy + Jx);
    Goto EXIT2;
  }

NOT_FKEY:
  Set_Global_Int('!FKEY@',0);

  Return_Str = Parse_Str('' + Kname + 'ð',
'ALT0ð129ALT-ð130ALT=ð131ALTQð16ALTWð17ALTEð18ALTRð19ALTTð20ALTYð21ALT''ð40ALT;ð39ALTUð22ALTIð23ALTOð24ALTPð25ALTAð30ALTSð31ALTDð32' +
'ALTFð33ALTGð34SHFTMEVENTð220SHFTMEVENT2ð221SHFTMEVENT3ð222CTRLMEVENTð223CTRLMEVENT3ð225');
  if(  (Return_Str != '')  ) {
    Goto EXIT2;
  }

  Return_Str = Parse_Str('' + Kname + 'ð',
'ALTHð35ALTJð36ALTKð37ALTLð38ALTZð44ALTXð45ALTCð46ALTVð47ALTBð48ALTSPACEð57ALTNð49ALTMð50LFð75RTð77UPð72DNð80HOMEð71ENDð79SHFTTABð15CTRLMEVENT2ð224' +
'CTRLLFð115CTRLRTð116CTRLHOMEð119CTRLENDð117CTRLPGUPð132CTRLPGDNð118');
  if(  (Return_Str != '')  ) {
    Goto EXIT2;
  }

  Return_Str = Parse_Str('' + Kname + 'ð',
'ALT1ð120ALT2ð121ALT3ð122ALT4ð123ALT5ð124ALT6ð125ALT7ð126ALT8ð127ALTMEVENTð226ALT9ð128INSð82DELð83F11ð133F12ð134SHFTF11ð135SHFTF12ð136CTRLF11ð137ALTMEVENT2ð227' +
'CTRLF12ð138BTN2ð246MSUPð240MSDNð241MSRTð243MSLFð242MEVENTð250');
  if(  (Return_Str != '')  ) {
    Goto EXIT2;
  }

  Return_Str = Parse_Str('' + Kname + 'ð',
'PGUPð73PGDNð81CTRLUPð141CTRLDNð145CTRLCNTRð143ALTESCð1MEVENT2ð251MEVENT3ð252CNTRð76ALT`ð41ALT<ð51ALT>ð52ALT?ð53CTRLINSð146ALTMEVENT3ð228' +
'CTRLDELð147CTRLGREY+ð144CTRLGREY-ð142ALTF11ð139ALTF12ð140BTN0ð244BTN1ð245CTRLGREY*ð150ALTGREY*ð55');
  if(  (Return_Str != '')  ) {
    Goto EXIT2;
  }

  Return_Str = Parse_Str('' + Kname + 'ð',
'ALTGREY-ð74ALTGREY+ð78ALTGREYENTERð166CTRLTABð148ALTTABð165ALTBSð14ALTENTERð28ALT[ð26ALT]ð27CTRL@ð3SCROLLLOCKONð238SCROLLLOCKOFFð239' +
'ALT\ð43ALTINSð162ALTDELð163ALTHOMEð151ALTENDð159ALTPGUPð153ALTPGDNð161ALTUPð152ALTDNð160ALTLFð155ALTRTð157ALTGREY/ð164CTRLGREY/ð149'
);
  if(  (Return_Str != '')  ) {
    Goto EXIT2;
  }

  Return_Str = Parse_Str('' + Kname + 'ð',
'ENTERð13/K2=28GREYENTERð13/K2=224CTRLMð13/K2=50CTRLENTERð10/K2=28' +
'CTRLJð10/K2=36ESCð27/K2=1CTRL[ð27/K2=26BSð8/K2=14/K2=1CTRLHð8/K2=35CTRLGREYENTERð10/K2=224TABð9/K2=15CTRLIð9/K2=237ð55/K2=8' +
'SHFTHOMEð55/K2=718ð56/K2=9SHFTUPð56/K2=729ð57/K2=10SHFTPGUPð57/K2=73CTRLSPACEð252/K2=57');
  if(  (Return_Str != '')  ) {
    Return_Str = '/K1=' + Return_Str;
    Goto EXIT;
  }


  Return_Str = Parse_Str('' + Kname + 'ð',
'-ð45/K2=12GREY-ð45/K2=74*ð42/K2=9GREY*ð42/K2=554ð52/K2=5SHFTLFð52/K2=755ð53/K2=6SHFTCNTRð53/K2=766ð54/K2=7SHFTRTð54/K2=77' +
'+ð43/K2=13GREY+ð43/K2=781ð49/K2=2SHFTENDð49/K2=792ð50/K2=3SHFTDNð50/K2=803ð51/K2=4SHFTPGDNð51/K2=810ð48/K2=11SHFTINSð48/K2=82.ð46/K2=52SHFTDELð46/K2=83GREY/ð47/K2=244CTRLBSð127/K2=14'
  );
  if(  (Return_Str != '')  ) {
    Return_Str = '/K1=' + Return_Str;
    Goto EXIT;
  }

  if(  ((XPos('CTRL',kname,1) == 1) & (Svl(Kname) == 5))  ) {
    Jx = Ascii(Str_Char(Kname,5));
    if(  ((Jx > 63) & (Jx < 96))  ) {
      TStr =
'|3|30|48|46|32|18|33|34|35|23|36|37|38|50|49|24|25|16|19|31|20|22|47|17|45' +
'|21|44|26|43|27|7|12';
      Return_Str = '/K1=' + str(jx - 64) +
                    '/K2=' + Str(Ascii(Str_Char(Tstr,Jx - 63)));
      Goto EXIT;
    }

  }

  if(  (Svl(Kname) == 1)  ) {
/* Alphanumeric */
    Jx = Ascii(Kname);
    if(  ((Jx > 31) & (Jx < 127))  ) {
      TStr =
'|57|2|40|4|5|6|8|40|10|11|9|13|51|12|52|53|11|2|3|4|5|6|7|8|9|10|39|39|51|13|52|53|3|30|48|46|32|18|33|34|35|23|36|37|38|50|49|24|25|16|19|31|20|22' +
'|47|17|45|21|44|26|43|27|7|12|41|30|48|46|32|18|33|34|35|23|36|37|38|50|49|24|25|16|19|31|20|22|47|17|45|21|44|26|43|27|41';
      Return_Str = '/K1=' + Str(Ascii(Kname)) +
                    '/K2=' + Str(Ascii(Str_Char(Tstr,Jx - 31)));
      Goto EXIT;
    }
  }

EXIT2:
  Return_Str = '/K1=0/K2='+ Return_Str;

EXIT:
  if(  (Return_Str != '')  ) {
    Return_Int = (Parse_Int('/K2=',Return_Str) << 8) |
      Parse_Int('/K1=',Return_Str);
  }
}

macro ASSIGN_CMD FROM ALL TRANS2 {
/*******************************************************************************
                                MULTI_EDIT MACRO

Name:  ASSIGN_CMD

Description:  Takes a valid command name (like DEL_CHAR) and assigns a valid
              key value to it.

Parameters:
              Return_Str should contain the command name
              Return_Int should contain the 2 byte key value

Returns:
              Return_Int = 1 if successful, 0 if not.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
  str Cmd[20] = Caps(Parse_Str('/C=',MParm_Str));
  int
          C_Mode = Parse_Int('/M=',MParm_Str),
          Key = Parse_Int('/K=',MParm_Str)
          ;

  Return_Int = 0;
  if(  (Key < 1)  ) {
    Goto EXIT;
  }
  if(  (Cmd == '')  ) {
    Goto EXIT;
  }
  if(  (C_Mode < 0)  ) {
    C_Mode = 0;
  }
  Return_Int = 1;

  if(  (Cmd == 'BACK_SPACE')  ) {
    Cmd_to_Key(Key,BACK_SPACE,C_Mode);
  } else if(  (Cmd == 'DEL_CHAR')  ) {
    Cmd_to_Key(Key,DEL_CHAR,C_Mode);
  } else if(  (Cmd == 'DEL_LINE')  ) {
    Cmd_to_Key(Key,DEL_LINE,C_Mode);
  } else if(  (Cmd == 'UNDO')  ) {
    Cmd_to_Key(Key,UNDO,C_Mode);
  } else if(  (Cmd == 'REDO')  ) {
    Cmd_to_Key(Key,REDO,C_Mode);
  } else if(  (Cmd == 'LEFT')  ) {
    Cmd_to_Key(Key,LEFT,C_Mode);
  } else if(  (Cmd == 'RIGHT')  ) {
    Cmd_to_Key(Key,RIGHT,C_Mode);
  } else if(  (Cmd == 'UP')  ) {
    Cmd_to_Key(Key,UP,C_Mode);
  } else if(  (Cmd == 'DOWN')  ) {
    Cmd_to_Key(Key,DOWN,C_Mode);
  } else if(  (Cmd == 'PAGE_UP')  ) {
    Cmd_to_Key(Key,PAGE_UP,C_Mode);
  } else if(  (Cmd == 'PAGE_DOWN')  ) {
    Cmd_to_Key(Key,PAGE_DOWN,C_Mode);
  } else if(  (Cmd == 'TOF')  ) {
    Cmd_to_Key(Key,TOF,C_Mode);
  } else if(  (Cmd == 'EOF')  ) {
    Cmd_to_Key(Key,EOF,C_Mode);
  } else if(  (Cmd == 'NEXT_PAGE_BREAK')  ) {
    Cmd_to_Key(Key,NEXT_PAGE_BREAK,C_Mode);
  } else if(  (Cmd == 'LAST_PAGE_BREAK')  ) {
    Cmd_to_Key(Key,LAST_PAGE_BREAK,C_Mode);
  } else if(  (Cmd == 'WORD_LEFT')  ) {
    Cmd_to_Key(Key,WORD_LEFT,C_Mode);
  } else if(  (Cmd == 'WORD_RIGHT')  ) {
    Cmd_to_Key(Key,WORD_RIGHT,C_Mode);
  } else if(  (Cmd == 'TAB_RIGHT')  ) {
    Cmd_to_Key(Key,TAB_RIGHT,C_Mode);
  } else if(  (Cmd == 'TAB_LEFT')  ) {
    Cmd_to_Key(Key,TAB_LEFT,C_Mode);
  } else if(  (Cmd == 'INDENT')  ) {
    Cmd_to_Key(Key,INDENT,C_Mode);
  } else if(  (Cmd == 'UNDENT')  ) {
    Cmd_to_Key(Key,UNDENT,C_Mode);
  } else if(  (Cmd == 'KEY_RECORD')  ) {
    Cmd_to_Key(Key,KEY_RECORD,C_Mode);

/* The remainder of these are not in any of our supplied keymaps.  If needed,
they be uncommented

  ELSIF (Cmd = 'BLOCK_BEGIN') THEN
    Cmd_to_Key(Key,BLOCK_BEGIN,C_Mode);
  ELSIF (Cmd = 'BLOCK_END') THEN
    Cmd_to_Key(Key,BLOCK_END,C_Mode);
  ELSIF (Cmd = 'BLOCK_OFF') THEN
    Cmd_to_Key(Key,BLOCK_OFF,C_Mode);
  ELSIF (Cmd = 'CALCULATOR') THEN
    Cmd_to_Key(Key,CALCULATOR,C_Mode);
  ELSIF (Cmd = 'COL_BLOCK_BEGIN') THEN
    Cmd_to_Key(Key,COL_BLOCK_BEGIN,C_Mode);
  ELSIF (Cmd = 'COPY_BLOCK') THEN
    Cmd_to_Key(Key,COPY_BLOCK,C_Mode);
  ELSIF (Cmd = 'CR') THEN
    Cmd_to_Key(Key,CR,C_Mode);
  ELSIF (Cmd = 'CREATE_WINDOW') THEN
    Cmd_to_Key(Key,CREATE_WINDOW,C_Mode);
  ELSIF (Cmd = 'DELETE_BLOCK') THEN
    Cmd_to_Key(Key,DELETE_BLOCK,C_Mode);
  ELSIF (Cmd = 'DELETE_WINDOW') THEN
    Cmd_to_Key(Key,DELETE_WINDOW,C_Mode);
  ELSIF (Cmd = 'ENTER_DOS_SHELL') THEN
    Cmd_to_Key(Key,ENTER_DOS_SHELL,C_Mode);
  ELSIF (Cmd = 'EOL') THEN
    Cmd_to_Key(Key,EOL,C_Mode);
  ELSIF (Cmd = 'ERASE_WINDOW') THEN
    Cmd_to_Key(Key,ERASE_WINDOW,C_Mode);
  ELSIF (Cmd = 'FIRST_WORD') THEN
    Cmd_to_Key(Key,FIRST_WORD,C_Mode);
  ELSIF (Cmd = 'GOTO_MARK') THEN
    Cmd_to_Key(Key,GOTO_MARK,C_Mode);
  ELSIF (Cmd = 'HOME') THEN
    Cmd_to_Key(Key,HOME,C_Mode);
  ELSIF (Cmd = 'MARK_POS') THEN
    Cmd_to_Key(Key,MARK_POS,C_Mode);
  ELSIF (Cmd = 'MODIFY_WINDOW') THEN
    Cmd_to_Key(Key,MODIFY_WINDOW,C_Mode);
  ELSIF (Cmd = 'MOVE_BLOCK') THEN
    Cmd_to_Key(Key,MOVE_BLOCK,C_Mode);
  ELSIF (Cmd = 'NEWSCREEN') THEN
    Cmd_to_Key(Key,NEWSCREEN,C_Mode);
  ELSIF (Cmd = 'POP_UNDO') THEN
    Cmd_to_Key(Key,POP_UNDO,C_Mode);
  ELSIF (Cmd = 'PUSH_UNDO') THEN
    Cmd_to_Key(Key,PUSH_UNDO,C_Mode);
  ELSIF (Cmd = 'QUIT') THEN
    Cmd_to_Key(Key,QUIT,C_Mode);
  ELSIF (Cmd = 'REDRAW') THEN
    Cmd_to_Key(Key,REDRAW,C_Mode);
  ELSIF (Cmd = 'REST_DOS_SCREEN') THEN
    Cmd_to_Key(Key,REST_DOS_SCREEN,C_Mode);
  ELSIF (Cmd = 'RIGHT') THEN
    Cmd_to_Key(Key,RIGHT,C_Mode);
  ELSIF (Cmd = 'SAVE_DOS_SCREEN') THEN
    Cmd_to_Key(Key,SAVE_DOS_SCREEN,C_Mode);
  ELSIF (Cmd = 'SAVE_FILE') THEN
    Cmd_to_Key(Key,SAVE_FILE,C_Mode);
  ELSIF (Cmd = 'UNLINK_WINDOW') THEN
    Cmd_to_Key(Key,UNLINK_WINDOW,C_Mode);
  ELSIF (Cmd = 'ZOOM') THEN
    Cmd_to_Key(Key,ZOOM,C_Mode);
 */
  } else {
    Return_Int = 0;
  }
EXIT:
}

macro UNASSKEY TRANS2{
/*******************************************************************************
                               MULTI-EDIT MACRO

Name:  UNASSKEY

Description:  Builds a list of currently unassigned keystrokes.

Parameters:
              /MODE=    The ME mode to check.  Legal values are 0(EDIT),
                        1(DOS_SHELL), and 3(TERM). Anything out of range will
                        default to 0.
              /ALPHA=   Enables inclusion of alphanumeric keys(Scan codes
                        32-126) in the list.  Probably not desireable, but
                        there if you need it.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

  str T_Str,K_Str[300];
  int Check_Mode,K1,K2,Counter,Alphanum,Active_Window,List_Window,
          T_Refresh;

  T_Refresh = Refresh;
  Refresh = False;
  Active_Window = Window_Id;
  Create_Window;
  List_Window = Window_Id;
  Check_Mode = Parse_Int('/MODE=',MParm_Str);
  if(  ((Check_Mode < 0) | (Check_Mode > 3) | (Check_Mode == 2))  ) {
    Check_Mode = 0;
  }
  Working;
  Make_Message('Building list of unassigned keys for ' +
    Remove_Space(Copy('EDIT     DOS_SHELL         TERM     ALL',
      (Check_Mode * 9) + 1, 9)) + ' mode.');
  Alphanum = Parse_Int('/ALPHA=',MParm_Str);

  K1 = 0;
  K2 = 0;
  while(  (K2 < 240)  ) {
    ++K2;
    Call CHECK_ASSIGNMENT;
  }

  K_Str = '|0|3|1|30|2|48|3|46|4|32|5|18|6|33|7|34|8|14|8|35|9|15|9|23|10|28|10|224|10|36|11|37|12|38|13|28|13|224|13|50|14|49|15|24|16|25|17|16|18|19|19|31|20|20|21|22|22|47|23|17|24|45|25|21|26|44|27|1|27|26|28|43|29|27|30|7|31|12|32|57|33|2' +
          '|34|40|35|4|36|5|37|6|38|8|39|40|40|10|41|11|42|9|42|55|43|13|43|78|44|51|45|12|45|74|46|52|46|83|47|53|47|224|48|11|48|82|49|2|50|3|50|80|51|4|51|81|52|5|52|75|53|6|54|7|54|77|55|8|56|9|56|72|57|10|57|73|58|39|59|39|60|51|61|13|62|52' +
          '|63|53|64|3|65|30|66|48|67|46|68|32|69|18|70|33|71|34|72|35|73|23|74|36|75|37|76|38|77|50|78|49|79|24|80|25|81|16|82|19|83|31|84|20|85|22|86|47|87|17|88|45|89|21|90|44|91|26|92|43|93|27|94|7|95|12|96|41|97|30|98|48|99|46|100|32|101|18' +
          '|102|33|103|34|104|35|105|23|106|36|107|37|108|38|109|50|110|49|111|24|112|25|113|16|114|19|115|31|116|20|117|22|118|47|119|17|120|45|121|21|122|44|123|26|124|43|125|27|126|41|127|14';
  Counter = 0;
  while(  (Counter < SVL(K_Str))  ) {
    ++Counter;
    K1 = ASCII(Str_Char(K_Str,Counter));
    ++Counter;
    K2 = ASCII(Str_Char(K_Str,Counter));
    Call CHECK_ASSIGNMENT;
  }

  Make_Message('');
  tof;
  Set_Global_Str('UAKIPARM_1','/TP=15/C=1/L=0/WIN=' + str(cur_window) );
  RM('USERIN^DATA_IN /A=2/T=UNASSIGNED KEYS/#=1/PRE=UAK/A=1');

  Goto EXIT;

CHECK_ASSIGNMENT:
  if(  (INQ_KEY( k1, k2, Check_Mode, T_Str ) == 0)  ) {
    Return_Str = '';
    RM('SETUP^MAKEKEY /K1=' + Str(K1) + '/K2=' + Str(K2));
    if(  (Return_Str != '')  ) {
    if(  (Length(Return_Str) > ((Alphanum == False) * 3))  ) {
/* The above condition discriminates against alphanumeric keys if Alphanum =
False */
      Put_Line(Return_Str);
      Down;
    }
  }
  }
  RET;

EXIT:
  if(  (Switch_Win_Id(List_Window))  ) {
    Delete_Window;
  }
  Switch_Win_Id(Active_Window);
  Refresh = T_Refresh;
}

macro BUILDKEY FROM EDIT TRANS2 {
/*******************************************************************************
                                MULTI-EDIT MACRO

Name:   BUILDKEY

Description:  This macro converts a keystroke macro into a KEY_IN macro
              statement and optionally creates an entire macro from it.

               (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

#ifdef DEMO
	RM('meutil3^notinc /H=CONVERT/T=CONVERT KEYSTROKE MACRO' + mparm_str);
#else
  int JX,
      T_Insert_Mode = Insert_Mode,
      menu = menu_create;
  str TStr[30],Macro_Name[16];

  Tstr = '/G=@CONVERT_TYPE@/C=2';
  menu_set_item(menu,1,"Convert to:","","/QK=1/L=1/C=1",10,0,0);
  menu_set_item(menu,2,"Key assignment","","/QK=1/L=2/C=30/W=15",9,0,0);
  menu_set_item(menu,3,"Complete Macro       ","",Tstr + "/L=2/R=1",12,1,0);
  menu_set_item(menu,4,"Key_In statement only","",Tstr + "/L=3/R=2",12,0,0);
  menu_set_item(menu,5," ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ Setup For COMPLETE MACRO Only ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ","","/QK=1/L=5/C=1",10,0,0);
  menu_set_item(menu,6,"Macro Name: ","","/QK=1/L=6/C=1/W=16/HISTORY=",0,0,0);
  Tstr = '/G=@TMODE@/L=8';
  menu_set_item(menu,7,"Mode:","","/QK=2/L=7/C=1",10,0,0);
  menu_set_item(menu,8,"Edit ","",Tstr + "/C=11/R=1",12,1,0);
  menu_set_item(menu,9,"Dos_Shell ","",Tstr + "/C=21/R=2",12,0,0);
  menu_set_item(menu,10,"Term ","",Tstr + "/C=36/R=3",12,0,0);
  menu_set_item(menu,11,"All ","",Tstr + "/C=46/R=4",12,0,0);

ASK_AGAIN:
  Return_Int = menu;
  RM('USERIN^DATA_IN /HN=1/PRE=BK/#=11/T=CONVERT KEYSTROKE MACRO');

  if (!return_int) {
    GOTO Exit;
  }
  if (menu_item_int(menu,2,2) == 0) {
    RM('MEERROR^MESSAGEBOX /M=YOU MUST ENTER AN KEY ASSIGNMENT!/B=2');
    GOTO ASK_AGAIN;
  }
  Key1 = menu_item_int(menu,2,2) & $FF;
  Key2 = (menu_item_int(menu,2,2) >> 8) & $FF;

  if (GLOBAL_INT('@CONVERT_TYPE@') == 1) {
    if (menu_item_str(menu,6,2) == '') {
     RM('MEERROR^MESSAGEBOX /M=YOU MUST ENTER A MACRO NAME!/B=2');
     GOTO ASK_AGAIN;
    }
    Macro_Name = Caps(menu_item_str(menu,6,2));

    Push_Undo;
    Create_Window;
    RM("SETWINDOWNAMES");
    File_Name =  Macro_Name + '.S';
    RM('EXTSETUP');
    RM('SETUP^MAKEKEY /K1='+Str(key1)+'/K2='+Str(key2));
    TStr = Return_Str;
    if (Copy(TStr,1,1) == '<') {
      TStr = Str_Del(Tstr,1,1);
    }
    if (Copy(TStr,svl(TStr),1) == '>') {
      Tstr = Str_Del(TStr,Svl(TStr),1);
    }

    TEXT('macro ' + MACRO_NAME + ' TO ' + RETURN_STR + ' FROM ' +
    remove_space(copy('EDIT     DOS_SHELLTERM     ALL',((GLOBAL_INT('@TMODE@')-1)*9) + 1,9)));
    text(' {');
    CR;
  }

  Text('Key_In(');
  Play_Key_Macro(key1,key2,MODE);
  while (Check_Key) {
    if (C_COL > 70) {
      CR;
      GOTO_COL(1);
    }
    RM('SETUP^MAKEKEY /K1='+Str(key1)+'/K2='+Str(key2));
    TStr = Return_Str;
    if (XPos(TStr,'><()',1)) {
      Text('<' + TStr + '>');
    } else {
      Text(Tstr);
    }
  }
  Text(');');
  Redraw;

 if (Global_Int('@CONVERT_TYPE@') == 1) {
    CR;
    TEXT('}');
    Tof;
    Make_Message('Invoke COMPILE to compile this macro.');
 }

 POP_Undo;
EXIT:
  set_Global_Int('@CONVERT_TYPE@',0);
  set_Global_Int('@TMODE@',0);
  Insert_Mode = T_Insert_Mode;
  menu_delete(menu);
#endif // DEMO

}