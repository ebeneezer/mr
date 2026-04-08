// Resource created on 10-20-93 10:45:20am, version 7.00dPN-386
// Do NOT modify this macro source.  All modifications should be done
// through the resource file RESOURCE.DB
macro_file RESOURCE;

macro _MN_MAIN  {
  menu_set_item( return_int, 1, "File", "", "/S=5/H=FL/ CMD=FILETYPE=5CS=0/M=MENU /MX=0/MN=FILE", 0, 0,0);
  menu_set_item( return_int, 2, "Edit", "", "/S=5/H=EDITMENU/ CMD=EDITTYPE=5CS=0/M=MENU /MX=0/MN=EDIT", 0, 0,0);
  menu_set_item( return_int, 3, "Window", "", "/S=5/H=WN/ CMD=WINDOWTYPE=5CS=0/M=MENU /MX=0/MN=WINDOW", 0, 0,0);
  menu_set_item( return_int, 4, "Block", "", "/S=5/H=BL/ CMD=BLOCKTYPE=5CS=0/M=MENU /MX=0/MN=BLOCK", 0, 0,0);
  menu_set_item( return_int, 5, "Search", "", "/S=5/H=SR/ CMD=SEARCHTYPE=5CS=0/M=MENU /MX=0/MN=SEARCH", 0, 0,0);
  menu_set_item( return_int, 6, "Text", "", "/S=5/H=TEXTMENU/ CMD=TEXTTYPE=5/M=MENU /MX=0/MN=TEXT", 0, 0,0);
  menu_set_item( return_int, 7, "Macro", "", "/S=5/H=MC/ CMD=MACROTYPE=5/M=MENU /MX=0/MN=MACRO", 0, 0,0);
  menu_set_item( return_int, 8, "User", "", "/S=5/H=USER/ CMD=MECONFIG.DB^USERTYPE=5CS=0/M=MENU /MX=1/MN=USER/DBF=MECONFIG.DB", 0, 0,0);
  menu_set_item( return_int, 9, "Other", "", "/S=5/H=OTHER/ CMD=OTHERTYPE=5/M=MENU /MX=0/MN=OTHER", 0, 0,0);
  menu_set_item( return_int, 10, "Help", "", "/S=5/H=HELPMENU/ CMD=HELPTYPE=5/M=MENU /MX=0/MN=HELP", 0, 0,0);
  return_str = "/#=10/HZ=1/H=MAINMENU";
}

macro _MN_FILE trans2   {
  menu_set_item( return_int, 1, "Open...", "", "/S=2/H=EDITNEW/NM=1/ CMD=MEUTIL1^LOADFILE /E=1TYPE=1QKID=!FM_KEY1", 0, 0,0);
  menu_set_item( return_int, 2, "Load...", "", "/S=2/H=LOADFILE/NM=1/ CMD=MEUTIL1^LOADFILETYPE=1QKID=!FM_KEY2", 0, 0,0);
  menu_set_item( return_int, 3, "Save", "", "/S=2/H=SAVEFILE/NM=1/ CMD=MEUTIL1^SAVEFILE /NP=1TYPE=1QKID=!FM_KEY3CS=0", (file_changed == FALSE), 0,0);
  menu_set_item( return_int, 4, "save file As...", "", "/S=2/H=RENAMEFILE/NM=1/ CMD=MEUTIL1^SAVEFILETYPE=1QKID=!FM_KEY4", 0, 0,0);
  menu_set_item( return_int, 5, "saVe all files", "", "/S=2/H=SAVEALLFILES/NM=1/ CMD=AUTOSAVETYPE=1QKID=!FM_KEY12", 0, 0,0);
  menu_set_item( return_int, 6, "Information...", "", "/S=2/H=FILEINFO/NM=1/ CMD=MEUTIL1^FILESTATTYPE=1QKID=!FM_KEY5", 0, 0,0);
  menu_set_item( return_int, 7, "", "", "/S=2/H=/NM=1/ TYPE=6", 1, 0,0);
  menu_set_item( return_int, 8, "Merge...", "", "/S=2/H=LOADBLOCK/NM=1/ CMD=MEUTIL1^SPLICETYPE=1QKID=!FM_KEY7", 0, 0,0);
  menu_set_item( return_int, 9, "Print...", "", "/S=2/H=PR/NM=1/ CMD=MEUTIL3^PRINTTYPE=1QKID=!PRINT_KEY", 0, 0,0);
  menu_set_item( return_int, 10, "File manager", "", "/S=2/H=DIRSHELL^*/NM=1/ CMD=DIRSHELLTYPE=1QKID=!FM_KEY10", 0, 0,0);
  menu_set_item( return_int, 11, "shell to DOS", "", "/S=2/H=RD/NM=1/ CMD=MEUTIL1^SHELLDOSTYPE=1QKID=!MIM_KEY12CS=0", 0, 0,0);
  menu_set_item( return_int, 12, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 13, "sessioN manager...", "", "/S=2/H=SESSIONS/NM=1/ CMD=SESSMGRTYPE=1QKID=!FM_KEY13CS=0", 0, 0,0);
  menu_set_item( return_int, 14, "eXit", "", "/S=2/H=EXIT/NM=1/ CMD=EXITTYPE=1QKID=!FM_KEY11CS=0", 0, 0,0);
  return_str = "/#=14/HZ=0/ED=0/H=FL/T=/M=";
}

macro _MN_EDIT trans2   {
  menu_set_item( return_int, 1, "Undo", "", "/S=2/H=UN/NM=1/ CMD=TEXT^UNDOTYPE=1QKID=!TXM_KEY1CS=0", 0, 0,0);
  menu_set_item( return_int, 2, "Redo", "", "/S=2/H=REDO/NM=1/ CMD=TEXT^UNDO /RTYPE=1QKID=!TXM_KEY2CS=0", 0, 0,0);
  menu_set_item( return_int, 3, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 4, "cuT to buffer", "", "/S=2/H=CPCUT/NM=1/ CMD=MEUTIL2^CUT /MTYPE=1QKID=!CPM_KEY2CS=0", (block_stat == 0 ), 0,0);
  menu_set_item( return_int, 5, "Copy to buffer", "", "/S=2/H=CPCOPY/NM=1/ CMD=MEUTIL2^CUTTYPE=1QKID=!CPM_KEY1CS=0", (block_stat == 0), 0,0);
  menu_set_item( return_int, 6, "Append to buffer", "", "/S=2/H=CPAPPEND/NM=1/ CMD=MEUTIL2^CUT /ATYPE=1QKID=!CPM_KEY3CS=0", (block_stat == 0), 0,0);
  menu_set_item( return_int, 7, "cut aNd append to buffer", "", "/S=2/H=CPAPPEND/NM=1/ CMD=MEUTIL2^CUT /A /MTYPE=1QKID=!CPM_KEY4CS=0", (block_stat == 0), 0,0);
  menu_set_item( return_int, 8, "Paste from buffer", "", "/S=2/H=CPCOPYFROM/NM=1/ CMD=MEUTIL2^PASTETYPE=1QKID=!CPM_KEY5CS=0", (Global_Int("!BUFFER_ACTIVE") == 0), 0,0);
  menu_set_item( return_int, 9, "Show buffer", "", "/S=2/H=CPSHOW/NM=1/ CMD=ShowClipboardTYPE=1QKID=!CPM_KEY6CHECKED=!CLIP_SHOWCS=0", (Global_Int("!BUFFER_ACTIVE") == 0), 0,0);
  menu_set_item( return_int, 10, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 11, "rEpeat command", "", "/S=2/H=CMDREPEAT/NM=1/ CMD=MEUTIL1^REPEATTYPE=1QKID=!TXM_KEY15CS=0", 0, 0,0);
  menu_set_item( return_int, 12, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 13, "copy to Windows", "", "/S=2/H=WIN3/NM=1/ CMD=WIN3^WIN3_CLIPBOARD /M=1TYPE=1QKID=!W3_KEY2CS=0", ((Win3_Status == 0) || !block_stat), 0,0);
  menu_set_item( return_int, 14, "paste From Windows", "", "/S=2/H=WIN3/NM=1/ CMD=WIN3^WIN3_CLIPBOARD /M=0TYPE=1QKID=!W3_KEY1CS=0", (Win3_Clipboard == 0), 0,0);
  return_str = "/#=14/HZ=0/ED=0/H=EDITMENU/T=/M=";
}

macro _MN_WINDOW trans2   {
  menu_set_item( return_int, 1, "Open...", "", "/S=2/H=WINCREATE/NM=1/ CMD=WINDOW^WINOP /T=0TYPE=1QKID=!WM_KEY1CS=0", 0, 0,0);
  menu_set_item( return_int, 2, "Close", "", "/S=2/H=WINDELETE/NM=1/ CMD=WINDOW^WINOP /T=1TYPE=1QKID=!WM_KEY2CS=0", 0, 0,0);
  menu_set_item( return_int, 3, "closE all", "", "/S=2/H=WINDELETEALL/NM=1/ CMD=WINDOW^WINOP /T=9TYPE=1QKID=!WM_KEY20CS=0", 0, 0,0);
  menu_set_item( return_int, 4, "Split...", "", "/S=2/H=WINSPLIT/NM=1/ CMD=WINDOW^WINOP /T=4TYPE=1QKID=!WM_KEY3OFFID=!WIN_DIS_1CS=0", 0, 0,0);
  menu_set_item( return_int, 5, "List...", "", "/S=2/H=WINLIST/NM=1/ CMD=WINDOW^WINOP /T=3TYPE=1QKID=!WM_KEY4CS=0", 0, 0,0);
  menu_set_item( return_int, 6, "", "", "/S=2/H=/NM=1/ TYPE=6", 1, 0,0);
  menu_set_item( return_int, 7, "Next", "", "/S=2/H=NEXTWIN/NM=1/ CMD=WINDOW^NEXTWINTYPE=1QKID=!WM_KEY6OFFID=!WIN_DIS_2CS=0", 0, 0,0);
  menu_set_item( return_int, 8, "Previous", "", "/S=2/H=PREVWIN/NM=1/ CMD=WINDOW^LASTWINTYPE=1QKID=!WM_KEY7OFFID=!WIN_DIS_2CS=0", 0, 0,0);
  menu_set_item( return_int, 9, "Adjacent...", "", "/S=2/H=ADJWIN/NM=1/ CMD=WINDOW^ADJACENT_WINTYPE=1QKID=!WM_KEY8OFFID=!WIN_DIS_3CS=0", 0, 0,0);
  menu_set_item( return_int, 10, "", "", "/S=2/H=/NM=1/ TYPE=6", 1, 0,0);
  menu_set_item( return_int, 11, "Hide", "", "/S=2/H=WINHIDE/NM=1/ CMD=WINDOW^WINOP /T=2TYPE=1QKID=!WM_KEY10CS=0", 0, 0,0);
  menu_set_item( return_int, 12, "Modify size", "", "/S=2/H=WINSIZE/NM=1/ CMD=WINDOW^WINOP /T=5TYPE=1QKID=!WM_KEY11CS=0", 0, 0,0);
  menu_set_item( return_int, 13, "Zoom", "", "/S=2/H=WINZOOM/NM=1/ CMD=WINDOW^WINOP /T=8TYPE=1QKID=!WM_KEY12CS=0", 0, 0,0);
  menu_set_item( return_int, 14, "mInimize", "", "/S=2/H=MINIMIZE/NM=1/ CMD=WINDOW^ZOOM /MINIMIZE=1TYPE=1QKID=!WM_KEY16OFFID=!WIN_DIS_1CS=0", 0, 0,0);
  menu_set_item( return_int, 15, "orGanize  ", "", "/S=5/H=ORGANIZE/ CMD=ORGANIZETYPE=5OFFID=!WIN_DIS_2CS=0/M=MENU /MX=0/MN=ORGANIZE", 0, 0,0);
  menu_set_item( return_int, 16, "Toggle screen layout", "", "/S=2/H=XZOOM/NM=1/ CMD=XZOOMTYPE=1QKID=!TOG_SCR_FORMCS=0", 0, 0,0);
  menu_set_item( return_int, 17, "", "", "/S=2/H=/NM=1/ TYPE=6", 1, 0,0);
  menu_set_item( return_int, 18, "linK...", "", "/S=2/H=WINLINK/NM=1/ CMD=WINDOW^WINOP /T=6TYPE=1QKID=!WM_KEY14CS=0", 0, 0,0);
  menu_set_item( return_int, 19, "Unlink", "", "/S=2/H=WINUNLINK/NM=1/ CMD=WINDOW^WINOP /T=7TYPE=1QKID=!WM_KEY15OFFID=!WIN_DIS_4", 0, 0,0);
  return_str = "/#=19/HZ=0/ED=0/H=WN/T=/M=WINDOW^WIN_MENU_SET";
}

macro _MN_BLOCK trans2   {
  menu_set_item( return_int, 1, "Copy block", "", "/S=2/H=BLOCKCOPY/NM=1/ CMD=MEUTIL2^BLOCKOP /BT=0TYPE=1QKID=!BM_KEY1CS=0", (block_stat == 0), 0,0);
  menu_set_item( return_int, 2, "Move block", "", "/S=2/H=BLOCKMOVE/NM=1/ CMD=MEUTIL2^BLOCKOP /BT=1TYPE=1QKID=!BM_KEY2CS=0", (block_stat == 0), 0,0);
  menu_set_item( return_int, 3, "Delete block", "", "/S=2/H=BLOCKDEL/NM=1/ CMD=MEUTIL2^BLOCKOP /BT=2TYPE=1QKID=!BM_KEY3CS=0", (block_stat == 0), 0,0);
  menu_set_item( return_int, 4, "", "", "/S=2/H=/NM=1/ TYPE=6", 1, 0,0);
  menu_set_item( return_int, 5, "save Block to disk", "", "/S=2/H=SAVEBLOCK/NM=1/ CMD=MEUTIL1^SaveBlckTYPE=1QKID=!FM_KEY8CS=0", (block_stat == 0), 0,0);
  menu_set_item( return_int, 6, "Indent block", "", "/S=2/H=BLOCKIND/NM=1/ CMD=MEUTIL2^IndBlkTYPE=1QKID=!BM_KEY7CS=0", (block_stat == 0), 0,0);
  menu_set_item( return_int, 7, "Undent block", "", "/S=2/H=BLOCKIND/NM=1/ CMD=MEUTIL2^UndBlkTYPE=1QKID=!BM_KEY8CS=0", (block_stat == 0), 0,0);
  menu_set_item( return_int, 8, "mAth operation", "", "/S=2/H=BLOCKMATH/NM=1/ CMD=MEUTIL2^BLCKMATHTYPE=1QKID=!BM_KEY13CS=0", (block_stat == 0), 0,0);
  menu_set_item( return_int, 9, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 10, "Window copy...", "", "/S=2/H=BLOCKWIN/NM=1/ CMD=MEUTIL2^BLOCKOP /BT=3TYPE=1QKID=!BM_KEY10CS=0", 0, 0,0);
  menu_set_item( return_int, 11, "wiNdow move...", "", "/S=2/H=BLOCKWIN/NM=1/ CMD=MEUTIL2^BLOCKOP /BT=4TYPE=1QKID=!BM_KEY11CS=0", 0, 0,0);
  menu_set_item( return_int, 12, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 13, "mark Lines of text", "", "/S=2/H=MARKLBLOCK/NM=1/ CMD=meutil2^MarkBlckTYPE=1QKID=!BM_KEY15CS=0", (marking != 0), ((global_int("@LAST_BLOCK_TYPE") == 1) * 2),0);
  menu_set_item( return_int, 14, "mark cOlumns of text", "", "/S=2/H=MARKCBLOCK/NM=1/ CMD=meutil2^MColBlckTYPE=1QKID=!BM_KEY16CS=0", (marking != 0), ((global_int("@LAST_BLOCK_TYPE") == 2) * 2),0);
  menu_set_item( return_int, 15, "mark Stream of text", "", "/S=2/H=MARKSBLOCK/NM=1/ CMD=meutil2^MStrBlckTYPE=1QKID=!BM_KEY17CS=0", (marking != 0), (((Global_Int("@LAST_BLOCK_TYPE") == 3) || (global_int("@LAST_BLOCK_TYPE") ==0)) * 2 ),0);
  menu_set_item( return_int, 16, "End marking", "", "/S=2/H=MARKLBLOCK/NM=1/ CMD=meutil2^MarkBlckTYPE=1QKID=!BM_KEY15CS=0", (marking == 0), 0,0);
  menu_set_item( return_int, 17, "turn marking oFf", "", "/S=2/H=BLOCKOFF/NM=1/ CMD=meutil2^BlockOffTYPE=1QKID=!BM_KEY18CS=0", (block_stat == 0), 0,0);
  menu_set_item( return_int, 18, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 19, "Persistent blocks", "", "/S=2/H=BLOCKPERSIST/NM=1/ CMD=Meutil2^blockop /BT=5TYPE=1CS=0", 0, (persistent_blocks != 0),0);
  return_str = "/#=19/HZ=0/ED=0/H=BL/T=/M=";
}

macro _MN_SEARCH trans2   {
  menu_set_item( return_int, 1, "Search for text...", "", "/S=2/H=SEARCH/NM=1/ CMD=MEUTIL2^SEARCHTYPE=1QKID=!SRM_KEY1CS=0", 0, 0,0);
  menu_set_item( return_int, 2, "search and Replace...", "", "/S=2/H=SRCHREPL/NM=1/ CMD=MEUTIL2^S_REPLTYPE=1QKID=!SRM_KEY2CS=0", 0, 0,0);
  menu_set_item( return_int, 3, "repeat Previous search", "", "/S=2/H=REPSEARCH/NM=1/ CMD=MEUTIL2^REPSRCHTYPE=1QKID=!SRM_KEY3CS=0", 0, 0,0);
  menu_set_item( return_int, 4, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 5, "multiple File search...", "", "/S=2/H=FILESEARCH/NM=1/ CMD=MEUTIL2^FSTYPE=1QKID=!SRM_KEY5CS=0", 0, 0,0);
  menu_set_item( return_int, 6, "List files from last search...", "", "/S=2/H=SEARCHLIST/NM=1/ CMD=MEUTIL2^FS /NTYPE=1QKID=!SRM_KEY6CS=0", 0, 0,0);
  menu_set_item( return_int, 7, "Multiple file search and replace...", "", "/S=2/H=FILESRCHREPL/NM=1/ CMD=MEUTIL2^FS /M=1TYPE=1QKID=!SRM_KEY7CS=0", 0, 0,0);
  menu_set_item( return_int, 8, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 9, "pUsh position onto marker stack", "", "/S=2/H=PMSTACK/NM=1/ CMD=TEXT^MARKPOSTYPE=1QKID=!CRSM_KEY1CS=0", 0, 0,0);
  menu_set_item( return_int, 10, "Get position from marker stack", "", "/S=2/H=PMSTACK/NM=1/ CMD=TEXT^GOTOMARKTYPE=1QKID=!CRSM_KEY2CS=0", 0, 0,0);
  menu_set_item( return_int, 11, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 12, "set random Access mark...", "", "/S=2/H=PMRANDOM/NM=1/ CMD=TEXT^SET_MARKTYPE=1QKID=!CRSM_KEY4CS=0", 0, 0,0);
  menu_set_item( return_int, 13, "reTrieve random access mark...", "", "/S=2/H=PMRANDOM/NM=1/ CMD=TEXT^GET_MARKTYPE=1QKID=!CRSM_KEY5CS=0", 0, 0,0);
  menu_set_item( return_int, 14, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 15, "goto line Number...", "", "/S=2/H=GOTOLINE/NM=1/ CMD=GOTOLINETYPE=1QKID=!CRSM_KEY7CS=0", 0, 0,0);
  return_str = "/#=15/HZ=0/ED=0/H=SR/M=";
}

macro _MN_TEXT trans2   {
  menu_set_item( return_int, 1, "Layout...", "", "/S=2/H=LAYOUT/NM=1/ CMD=MEUTIL3^LAYOUTTYPE=1QKID=!TXM_KEY16CS=0", 0, 0,0);
  menu_set_item( return_int, 2, "heX mode", "", "/S=2/H=HEX/NM=1/ CMD=Hex^HexWindowTYPE=1QKID=!HEX_MODECS=0", 0, (xpos("Hex", event_macro,1)),0);
  menu_set_item( return_int, 3, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 4, "coMpare...", "", "/S=2/H=COMPARE/NM=1/ CMD=FCTYPE=1QKID=!FC_KEY1CS=0", 0, 0,0);
  menu_set_item( return_int, 5, "Sort text...", "", "/S=2/H=SORTTEXT/NM=1/ CMD=MEUTIL3^SORTMENUTYPE=1QKID=!TXM_KEY4CS=0", 0, 0,0);
  menu_set_item( return_int, 6, "upper Case  ", "", "/S=5/H=CNGCASE/ CMD=UPPER_CASETYPE=5QKID=!TXM_KEY5CS=0/M=MENU /MX=0/MN=UPPER_CASE", 0, 0,0);
  menu_set_item( return_int, 7, "loWer case  ", "", "/S=5/H=CNGCASE/ CMD=LOWER_CASETYPE=5QKID=!TXM_KEY6CS=0/M=MENU /MX=0/MN=LOWER_CASE", 0, 0,0);
  menu_set_item( return_int, 8, "cenTer line", "", "/S=2/H=CENTERLN/NM=1/ CMD=TEXT^CENTERTYPE=1QKID=!TXM_KEY7CS=0", 0, 0,0);
  menu_set_item( return_int, 9, "time/Date stamp", "", "/S=2/H=DT/NM=1/ CMD=DATETIMETYPE=1QKID=!TXM_KEY8CS=0", 0, 0,0);
  menu_set_item( return_int, 10, "rulEr", "", "/S=2/H=RULER/NM=1/ CMD=TEXT^RULERTYPE=1QKID=!TXM_KEY9CS=0", 0, 0,0);
  menu_set_item( return_int, 11, "paGe break", "", "/S=2/H=PG/NM=1/ CMD=PAGEBRKTYPE=1QKID=!INS_PB_KEYCS=0", 0, 0,0);
  menu_set_item( return_int, 12, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 13, "re-Format paragraph", "", "/S=2/H=REFORMAT/NM=1/ CMD=TEXT^REFORMATTYPE=1QKID=!TXM_KEY11CS=0", 0, 0,0);
  menu_set_item( return_int, 14, "Justify paragraph", "", "/S=2/H=JUSTIFY/NM=1/ CMD=TEXT^JUSTIFYTYPE=1QKID=!TXM_KEY12CS=0", 0, 0,0);
  menu_set_item( return_int, 15, "uN-justify paragraph", "", "/S=2/H=UNJUST/NM=1/ CMD=TEXT^UNJUSTIFTYPE=1QKID=!TXM_KEY13CS=0", 0, 0,0);
  return_str = "/#=15/H=TEXTMENU";
}

macro _MN_MACRO trans2   {
  menu_set_item( return_int, 1, "Run a macro...", "", "/S=2/H=RUNMAC/NM=1/ CMD=MEUTIL1^RUNMACTYPE=1QKID=!MCM_KEY1CS=0", 0, 0,0);
  menu_set_item( return_int, 2, "Load a macro file...", "", "/S=2/H=LOADMAC/NM=1/ CMD=MEUTIL3^LOADMACROTYPE=1QKID=!MCM_KEY2CS=0", 0, 0,0);
  menu_set_item( return_int, 3, "Debug a macro...", "", "/S=2/H=MEDEBUG^*/NM=1/ CMD=MEDEBUG^DEBUGTYPE=1QKID=!MCM_KEY3CS=0", 0, 0,0);
  menu_set_item( return_int, 4, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 5, "list All macros", "", "/S=2/H=LISTMACS/NM=1/ CMD=MEUTIL3^LISTMACSTYPE=1QKID=!MCM_KEY5CS=0", 0, 0,0);
  menu_set_item( return_int, 6, "list all Globals", "", "/S=2/H=LISTGLOBS/NM=1/ CMD=MEUTIL3^LISTGLOBTYPE=1QKID=!MCM_KEY6CS=0", 0, 0,0);
  menu_set_item( return_int, 7, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 8, "Keystroke macros...", "", "/S=2/H=KM/NM=1/ CMD=KEYMAC^KEYMAC_LISTTYPE=1QKID=!MCM_KEY8CS=0", 0, 0,0);
  return_str = "/#=8/H=MC";
}

macro _MN_OTHER trans2   {
  menu_set_item( return_int, 1, "Installation and setup", "", "/S=2/H=IN/NM=1/ CMD=SETUPTYPE=1QKID=!MIM_KEY2CS=0", 0, 0,0);
  menu_set_item( return_int, 2, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 3, "Execute compiler...", "", "/S=2/H=CP/NM=1/ CMD=LANGUAGE^COMPILETYPE=1QKID=!MIM_KEY4CS=0", 0, 0,0);
  menu_set_item( return_int, 4, "find neXt compiler error", "", "/S=2/H=CP%FIND NEXT COMPILER ERROR/NM=1/ CMD=LANGUAGE^CMPERRORTYPE=1QKID=!MIM_KEY5CS=0", 0, 0,0);
  menu_set_item( return_int, 5, "build Template", "", "/S=2/H=LANGUAGE/NM=1/ CMD=LANGUAGE^TEMPLATETYPE=1QKID=!MIM_KEY6CS=0", 0, 0,0);
  menu_set_item( return_int, 6, "Match language structure", "", "/S=2/H=LANGUAGE/NM=1/ CMD=LANGUAGE^MATCHTYPE=1QKID=!MIM_KEY7CS=0", 0, 0,0);
  menu_set_item( return_int, 7, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 8, "Calculator", "", "/S=2/H=CA/NM=1/ CMD=CALCTYPE=1QKID=!MIM_KEY9CS=0", 0, 0,0);
  menu_set_item( return_int, 9, "Ascii table", "", "/S=2/H=AS/NM=1/ CMD=SUPPORT^ASCIITYPE=1QKID=!MIM_KEY10CS=0", 0, 0,0);
  menu_set_item( return_int, 10, "coNdense display", "", "/S=2/H=CN/NM=1/ CMD=SUPPORT^CONDENSETYPE=1QKID=!MIM_KEY11CS=0", 0, 0,0);
  menu_set_item( return_int, 11, "Linedraw", "", "/S=2/H=LD/NM=1/ CMD=SUPPORT^LINEDRAWTYPE=1QKID=!MIM_KEY13CS=0", 0, 0,0);
  menu_set_item( return_int, 12, "", "", "/S=2/H=/NM=1/ TYPE=6CS=0", 1, 0,0);
  menu_set_item( return_int, 13, "teRminal", "", "/S=2/H=MECOM^*/NM=1/ CMD=MECOM^COMTYPE=1QKID=!MIM_KEY15CS=0", 0, 0,0);
  menu_set_item( return_int, 14, "Spell check  ", "", "/S=5/H=SPELL^*/ CMD=SPELL^SPELLTYPE=5QKID=!MIM_KEY16CS=0/M=MENU /MX=0/MN=SPELL/DBF=SPELL", 0, 0,0);
  menu_set_item( return_int, 15, "Print formatter", "", "/S=2/H=PRNFORM^PF/NM=1/ CMD=DOCUMENT^FPRINTTYPE=1CS=0", 0, 0,0);
  menu_set_item( return_int, 16, "Format codes", "", "/S=2/H=PRNFORM^FMTCODES/NM=1/ CMD=DOCUMENT^FCMDTYPE=1CS=0", 0, 0,0);
  return_str = "/#=16/H=OTHER";
}

macro _MN_HELP trans2   {
  menu_set_item( return_int, 1, "Table of contents", "", "/S=2/H=HM_TABLE/NM=1/ CMD=MEHELP /F=ME/LK=*/CX=1TYPE=1QKID=!HM_KEY1CS=0", 0, 0,0);
  menu_set_item( return_int, 2, "Keys", "", "/S=2/H=ACI_HYPE^QUICKREF/NM=1/ CMD=MEHELP /F=QUICKREF/LK=*/CX=1TYPE=1QKID=!HM_KEY6CS=0", 0, 0,0);
  menu_set_item( return_int, 3, "detailed Index", "", "/S=2/H=HM_INDEX/NM=1/ CMD=MEHELP /F=ME/LK=INDEX/CX=1TYPE=1QKID=!HM_KEY2CS=0", 0, 0,0);
  menu_set_item( return_int, 4, "Previous topic", "", "/S=2/H=HM_PREV/NM=1/ CMD=MEHELP /POP=1/CX=1TYPE=1QKID=!HM_KEY3CS=0", 0, 0,0);
  menu_set_item( return_int, 5, "Book marks...", "", "/S=2/H=HM_BOOK/NM=1/ CMD=MEHELP^HELP_LIST_MARKSTYPE=1QKID=!HM_KEY4CS=0", 0, 0,0);
  menu_set_item( return_int, 6, "Extension specific", "", "/S=2/H=HM_EXT/NM=1/ CMD=MEHELP /EXTHELP=1/CX=1TYPE=1QKID=!HM_KEY5CS=0", 0, 0,0);
  menu_set_item( return_int, 7, "About...", "", "/S=2/H=HM_ABOUT/NM=1/ CMD=MEUTIL3^ABOUTMETYPE=1CS=0", 0, 0,0);
  return_str = "/#=7/HZ=0/ED=0/H=HELPMENU/M=";
}

macro _MN_ORGANIZE trans2   {
  menu_set_item( return_int, 1, "Cascade", "", "/S=2/H=/NM=1/ CMD=WINDOW^WORGANIZE /M=0TYPE=1", 0, 0,0);
  menu_set_item( return_int, 2, "Tile", "", "/S=2/H=/NM=1/ CMD=WINDOW^WORGANIZE /M=1TYPE=1", 0, 0,0);
  menu_set_item( return_int, 3, "Minimize", "", "/S=2/H=/NM=1/ CMD=WINDOW^WORGANIZE /M=2/BTM=1TYPE=1CS=0", 0, 0,0);
  return_str = "/#=3/H=ORGANIZE";
}

macro _MN_UPPER_CASE trans2   {
  menu_set_item( return_int, 1, "Line", "", "/S=2/H=/NM=1/ CMD=TEXT^CHNGCASE /T=0/U=1TYPE=1CS=0", 0, 0,0);
  menu_set_item( return_int, 2, "Word", "", "/S=2/H=/NM=1/ CMD=TEXT^CHNGCASE /T=1/U=1TYPE=1CS=0", 0, 0,0);
  menu_set_item( return_int, 3, "Block", "", "/S=2/H=/NM=1/ CMD=TEXT^CHNGCASE /T=2/U=1TYPE=1CS=0", 0, 0,0);
  return_str = "/#=3/HZ=0/ED=0/H=CNGCASE/M=";
}

macro _MN_LOWER_CASE trans2   {
  menu_set_item( return_int, 1, "Line", "", "/S=2/H=/NM=1/ CMD=TEXT^CHNGCASE /T=0TYPE=1CS=0", 0, 0,0);
  menu_set_item( return_int, 2, "Word", "", "/S=2/H=/NM=1/ CMD=TEXT^CHNGCASE /T=1TYPE=1CS=0", 0, 0,0);
  menu_set_item( return_int, 3, "Block", "", "/S=2/H=/NM=1/ CMD=TEXT^CHNGCASE /T=2TYPE=1CS=0", 0, 0,0);
  return_str = "/#=3/HZ=0/ED=0/H=CNGCASE/M=";
}

macro _DG_LAYOUT trans2   {
  menu_set_item( return_int, 1, "Edit Mode:", "", "/C=1/L=1/QK=1/L=/C=/W=", 10, 0, 0);
  menu_set_item( return_int, 2, "Text     ", "", "/C=2/L=2/L=/C=/W=", 12, 0, 0);
  menu_set_item( return_int, 3, "Document ", "", "/C=2/L=3/L=/C=/W=", 12, 0, 0);
  menu_set_item( return_int, 4, "Indent style:", "", "/C=18/L=1/QK=1/L=/C=/W=", 10, 0, 0);
  menu_set_item( return_int, 5, "Off    ", "", "/C=19/L=2/L=/C=/W=", 12, 0, 0);
  menu_set_item( return_int, 6, "Auto   ", "", "/C=19/L=3/L=/C=/W=", 12, 0, 0);
  menu_set_item( return_int, 7, "Smart  ", "", "/C=19/L=4/L=/C=/W=", 12, 0, 0);
  menu_set_item( return_int, 8, "Tab spacing:", "", "/W=3/MIN=1/MAX=255/C=34/L=1/QK=1/L=/C=/W=", 1, 0, 0);
  menu_set_item( return_int, 9, "Right margin:", "", "/W=4/MIN=1/MAX=2048/C=51/L=1/QK=1/L=/C=/W=", 1, 0, 0);
  menu_set_item( return_int, 10, "Word wrap                  ", "", "/C=35/L=3/QK=1/L=/C=/W=", 13, 0, 0);
  menu_set_item( return_int, 11, "Save format line with file ", "", "/C=35/L=4/QK=1/L=/C=/W=", 13, 0, 0);
  menu_set_item( return_int, 12, "Display tabs               ", "", "/C=35/L=5/QK=1/L=/C=/W=", 13, 0, 0);
  menu_set_item( return_int, 13, "Tab settings mode:", "", "/QK=3/L=6/C=1/L=/C=/W=", 10, 0, 0);
  menu_set_item( return_int, 14, "Using tab and margin settings - ignoring format line", "", "/C=2/L=7/L=/C=/W=", 12, 0, 0);
  menu_set_item( return_int, 15, "Using format line - ignoring tab and margin settings", "", "/C=2/L=8/L=/C=/W=", 12, 0, 0);
  menu_set_item( return_int, 16, "Edit format line", "", "/QK=6/L=10/C=1/R=10/L=/C=/W=", 11, 0, 0);
  menu_set_item( return_int, 17, "Auto setup", "", "/QK=1/L=10/C=19/R=11/L=/C=/W=", 11, 0, 0);
  return_str = "/#=17/T=LAYOUT/H=LAYOUT";
}

macro _DG_WIN_LIST trans2   {
  menu_set_item( return_int, 1, "Delete", "", "/KC=<DEL>/K1=0/K2=83/R=12/L=1/C=4/L=1/C=4/W=0", 11, 0, 0);
  menu_set_item( return_int, 2, "Save", "", "/KC=<F3>/K1=0/K2=61/R=3/L=1/C=17/L=1/C=17/W=0", 11, 0, 0);
  menu_set_item( return_int, 3, "Hide window", "", "/KC=<F4>/K1=0/K2=62/R=4/L=1/C=27/L=1/C=27/W=0", 11, 0, 0);
  menu_set_item( return_int, 4, "Hide all", "", "/KC=<F5>/K1=0/K2=63/R=5/L=1/C=44/L=1/C=44/W=0", 11, 0, 0);
  menu_set_item( return_int, 5, "Preview", "", "/KC=<F6>/K1=0/K2=64/R=6/C=58/L=1/L=1/C=58/W=0", 11, 0, 0);
  menu_set_item( return_int, 6, "Select window:", "", "/DC=1/W=72/L=3/C=1/L=3/C=1/W=72", 15, 0, 0);
  return_str = "/#=6/T=WINDOW LIST/H=WINLIST";
}
