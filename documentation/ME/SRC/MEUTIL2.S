macro_file MEUTIL2;

#define LINBLOCK		1
#define COLBLOCK		2
#define STRBLOCK		3

#include mesys.sh

global
{
	int reg_exp_style;    // TRUE = Unix style expressions
}

/*******************************************************************************
														MULTI-EDIT MACRO FILE

Name: MEUTIL2

S_AND_R						- The general purpose search and search/replace macro.
SEARCH_LIST TRANS	- Brings up the list of occurrences created by search.
SRCH_ACROSS_LINE	- Performs Word/Phrase search
REPL_ACROSS_LINE	- Performs Word/Phrase replace
SRCHINTR					- Generates a dialog box for S_AND_R.
SEARCH						- This is the user interface for a SEARCH.
S_REPL						- This is the user interface for a SEARCH AND REPLACE.
REPSRCH						- This repeats the last search or search/replace.
FS								- The interface to multi-file search
FILESRCH					- Performs the actual multi-file search
UNDBLK						- Undent block macro.
INDBLK						- Indent block macro.
MARKBLCK					- Mark line block macro.
MCOLBLCK					- Mark columnar block macro.
MSTRBLCK					- Mark stream of text block macro.
BLOCKOFF					- Turn block mark off.
BLOCKOP						- Move, copy, and delete block operations.
SHOWCLIPBOARD			- Shows the  buffer(clipboard) in a window.
CUT								- Copies, Moves or Appends text into a hidden buffer.
PASTE							- Copies block from buffer into current file.
BLCKMATH					- Performs math operations on blocks of text.

								Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

macro S_AND_R NO_BREAK trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: S_AND_R

Description:  Search and search/replace interface.

Parameters:
							/BC=  Number of boxes to kill after exiting

Returns:      Return_Int = 0 if <ESC> was pressed at the prompts.
							Return_Int = 1 if search was carried through.
							Global_Int('FOUND_COUNT') = # of occurrences found.

Globals used:

							Global_Str('Switches')  	The search switches:
																				I - Ignore case
																				G - Global, search entire file
																				N - No prompts, search and replace
																						unconditionally
																				P - Prompt for repeated searches/replaces
																				B - Search backwards
																				X - Regular expressions off.  Faster if
																						regs not needed.
																				R - Restrict search to marked block
																				C - Don't move cursor to the right after
																						a replace has occurred between
																						multiple searches and replaces.
																						Applies only to search and replace.
																				W - Word/Phrase search
																				L - List all occurrences
																				A - search all windows
							Global_Str('Repl_Switches') Replace switches - See above.
							GLOBAL_STR('SEARCH_STR')	String to search for.
							GLOBAL_STR('REPLACE_STR')	String to replace with.
							GLOBAL_INT('SEARCH_MODE')	Mode 0=search 1=seach and replace
							GLOBAL_INT('REPSEARCH')		Simply repeat search or search/replace
																				according to previous search.  No box
																				with prompts.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int FoundNum, count, jm, jz, JX, Start_Row, Box_Ofs,t_col, kill_count, tp,
			hl,Back_Search, Search_Mode, Restrict, Restrict_Count, orig_line,
			Active_Window = Window_Id,
			first_window = window_id,
			List_Window_Num = 0,
			tpb = persistent_blocks,
			Making_List = false,
			file_replace = (parse_int("/FR=",mparm_str) > 0),
			all_files = 0,
			ev_count = 5 + (file_replace * 2),
			t_status_row = status_row,
			search_all_windows = 0,
			all_windows_count = 0,
			all_windows_foundnum = 0,
			all_windows_global = 0,
			t_tab_expand = tab_expand,
			s_flags = 0  // TMJ - 06-15-93 12:58pm - For Unix style expressions
			 ;
	int  GLOBALSEARCH, noprompt, S_Prompt,Searching,temp_ins, S_RES ,
					No_Cursor_Movement,New_Line_Num, Across_Lines,
					at_top = 0;
	str  Search_Str,Replace_Str,SpareString[200], Switches_Str[20], help_str[20],
					Interface_Mac ,
					list_File_Name[128];

	persistent_blocks = TRUE;
	kill_count = parse_int('/BC=', mparm_str );
	start_row = parse_int('/Y=', mparm_str);
	Box_Ofs = 0;

	Push_Undo;
	help_str = 'SEARCH';
	Search_Str = GLOBAL_STR('SEARCH_STR');
	Replace_Str = GLOBAL_STR('REPLACE_STR');
	Search_Mode = GLOBAL_INT('SEARCH_MODE');

	if (file_replace) {
		if(search_mode) {
			sparestring = 'FRSWITCHES';
		} else {
			sparestring = 'FSWITCHES';
		}
	} else {
		if(  search_mode  ) {
			sparestring = 'REPL_SWITCHES';
		} else {
			sparestring = 'SWITCHES';
		}
	}
	Switches_Str = caps(Global_Str(sparestring));

	FoundNum = 0;
	count = 0;

	if(  GLOBAL_INT('REPSEARCH') == 0  ) {
		jx = xpos( 'R', switches_str, 1 );
		if( !tpb && block_stat && !jx) {
			switches_str += 'R';
		}
		else if( !tpb && !block_stat && jx ) {
			switches_str = str_del( switches_str, jx, 1 );
		}
		set_global_str( sparestring, switches_str );
		if(  Search_Mode  ) {
			Box_Ofs = 1;
		}
		if(  search_mode  ) {
			help_str = 'SRCHREPL';
		}
		Set_Global_Int('MENU_RESULT',FALSE);

/* This allows the user to call an alternate search interface */
		Interface_Mac = Global_Str('@SRCH_INTR_MACRO');
		if(  (Interface_Mac == '')  ) {
			Interface_Mac = 'SRCHINTR ';
		}
		if(  (XPOS(' ',Interface_Mac,1) == 0)  ) {
			Interface_Mac = Interface_Mac + ' ';
		}
		refresh = false;
		mark_pos;
		if(  (c_line == block_line1) &
				(block_stat > 0) & (block_line1 == block_line2)  ) {
			if(  (block_stat == 1)  ) {
				first_word;
				set_global_str('SEARCH_HISTORY0', get_word('') );
			} else {
				set_global_str('SEARCH_HISTORY0', copy(get_line,block_col1, (block_col2 - block_col1) + 1));
			}
/*
			} else if(  (block_stat == 2)  ) {
				set_global_str('SEARCH_HISTORY0', copy(get_line,block_col1, (block_col2 - block_col1) + 1));
			} else if(  (block_stat == 3)  ) {
				set_global_str('SEARCH_HISTORY0', copy(get_line,block_col1, (block_col2 - block_col1) + 1));
			}
*/
		} else {
			right;
			word_left;
			set_global_str('SEARCH_HISTORY0', get_word(word_delimits) );
		}
		goto_mark;

			/* Call the search/replace user interface macro */
		RM(Interface_Mac + '/M=' + str( search_mode ) + '/H=' + help_str +
					'/Y=' + str(start_Row) + '/SRG=SEARCH_STR/RPG=REPLACE_STR/SWG='
				+ sparestring);

		if (return_Int == 12) {
			RM("SEARCH_LIST");
			Goto S_AND_R_EXIT;
		}
		set_global_str('SEARCH_HISTORY0', '' );
		search_str = Global_Str('SEARCH_STR');
		replace_str = Global_Str('REPLACE_STR');
		switches_str = Global_Str( sparestring );
		if(  NOT( Return_Int )  ) {
			Goto S_AND_R_EXIT;
		}
	}

	Return_Int = True;
	while(  box_count > kill_count  ) {
		kill_box;
	}
	refresh = true;

Start_Search:
	Switches_Str = CAPS( Switches_Str );
	Ignore_Case = (XPos('I',Switches_Str,1) != 0);

	GlobalSearch = (XPos('G',Switches_Str,1) != 0);
	if(  Global_Int('REPSEARCH') != 0  ) {
		GlobalSearch = false;
	}
	if (search_all_windows && all_windows_global) {
		globalsearch = true;
	}
	NoPrompt = (XPos('N',Switches_Str,1) != 0);
	Making_List = (XPos('L',Switches_Str,1) != 0);
	S_Prompt = (XPos('P',Switches_Str,1) != 0);
	Back_Search = (XPos('B',Switches_Str,1) != 0);
	Across_Lines = (XPos('W',Switches_Str,1) != 0);
//search_all_windows is not compatible with file_replace
	Search_All_Windows = ((XPos('A',Switches_Str,1) != 0) && (file_replace == 0));
	No_Cursor_Movement = False;
/*
beep;
make_message(str(noprompt) + ' ' + str(foundnum));
read_key;
*/
	if (Search_Mode == True) {
		Making_List = False;
		SpareString = Search_Str;
		Return_Str = Replace_Str;
		if(  (Ignore_Case)  ) {
			SpareString = Caps(SpareString);
			Return_Str = Caps(Return_Str);
		}
		if(  (SpareString != Return_Str)  ) {
/* Don't allow 'C' switch if search_str = replace_str.  This would cause a
continuous loop */
			No_Cursor_Movement = (XPos('C',Switches_Str,1) != 0);
		}
		if (file_replace) {
			status_row = 0;
			Set_Global_Str('!SREV#1', '/T=Repl/KC=<ENTER>/W=11/K1=13/K2=28/R=1/Y=' + str(message_row) + '/X=' + str(message_col));
			Set_Global_Str('!SREV#2', '/T=Repl/KC=<ENTER>/W=11/K1=13/K2=224/R=1/Y=' + str(message_row) + '/X=' + str(message_col));
			Set_Global_Str('!SREV#3', '/T= Skip/KC=<SP>/W=9/K1=32/K2=57/R=2/Y=' + str(message_row) + '/X=' + str(message_col + 11));
			Set_Global_Str('!SREV#4', '/T= Skip-File/KC=<AltS>/W=16/K1=0/K2=31/R=6/Y=' + str(message_row) + '/X=' + str(message_col + 20));
			Set_Global_Str('!SREV#5', '/T= Abort/KC=<ESC>/W=11/K1=27/K2=1/R=3/Y=' + str(message_row) + '/X=' + str(message_col + 36));
			Set_Global_Str('!SREV#6', '/T= Non-Stop/KC=<CtrlN>/W=16/K1=14/K2=49/R=4/Y=' + str(message_row) + '/X=' + str(message_col + 47));
			Set_Global_Str('!SREV#7', '/T= All-Files/KC=<CtrlA>/W=16/K1=1/K2=30/R=5/Y=' + str(message_row) + '/X=' + str(message_col + 63));
		} else {
			Set_Global_Str('!SREV#1', '/T=Replace/KC=<ENTER>/W=14/K1=13/K2=28/R=1/Y=' + str(message_row) + '/X=' + str(message_col));
			Set_Global_Str('!SREV#2', '/T= Skip/KC=<Space>/W=12/K1=32/K2=57/R=2/Y=' + str(message_row) + '/X=' + str(message_col + 14));
			Set_Global_Str('!SREV#3', '/T= Abort/KC=<ESC>/W=11/K1=27/K2=1/R=3/Y=' + str(message_row) + '/X=' + str(message_col + 26));
			Set_Global_Str('!SREV#4', '/T= Non-Stop/KC=<CtrlN>/W=16/K1=14/K2=49/R=4/Y=' + str(message_row) + '/X=' + str(message_col + 37));
			Set_Global_Str('!SREV#5', '/T=Replace/KC=<ENTER>/W=14/K1=13/K2=224/R=1/Y=' + str(message_row) + '/X=' + str(message_col));
		}

	} else {
		if (noprompt) {
			making_list = true;
		}
		if (making_list) {
			noprompt = true;
		}
		Set_Global_Str('!SREV#1', '/T=Search again/KC=<Any key>/W=21/K1=13/K2=28/R=1/Y=' + str(message_row) + '/X=' + str(message_col));
		Set_Global_Str('!SREV#2', '/T= Abort/KC=<ESC>/W=11/K1=27/K2=1/R=2/Y=' + str(message_row) + '/X=' + str(message_col + 21));
	}
	Reg_Exp_Stat = (XPos('X',Switches_Str,1) == 0);
	restrict_count = 0;
	orig_line = c_line;


	// 06-15-93 12:57pm
	// TMJ - Mods for UNIX style regular expressions
	if(reg_exp_stat)
	{
		if(reg_exp_style)
			s_flags |= _RegExp;
		else
			s_flags |= _OldExp;
	}
	if(!ignore_case)
		s_flags |= _CaseSensitive;
	if(back_search)
		s_flags |= _Backward;

	Mark_Pos;
	Restrict = XPos('R',Switches_Str,1) != 0;
	if(  (Restrict)  ) {
		block_end;
		if(  block_stat == 0  ) {
			if (search_all_windows) {
				goto SEARCH_OUT;
			}
			RM('USERIN^VERIFY /H=SR/BL=NO BLOCK MARKED/T=Perform search on file?');
			if(  return_int == 0  ) {
				make_message('No block marked.');
				refresh = true;
				redraw;
				Error_level = 0;
				goto s_and_r_exit;
			}
			restrict = 0;
		}
		else if( ((c_line == block_line1) && (c_col < block_col1)) ||
					((c_line == block_line2) && (c_col >= block_col2)) ||
					(c_line < block_line1) || (c_line > block_line2) ||
					(!tpb)) {
			Goto Restrict1;
		}
	}

	if(  GlobalSearch == TRUE  ) {
	Restrict1:
		if(  Back_Search  ) {
			if(  Restrict  ) {
				Goto_Line( Block_Line2 );
				EOL;
			} else {
				EOF;
			}
		} else {
			if(  Restrict  ) {
				Goto_Line( Block_Line1 );
				Goto_Col( 1 );
				goto_col( block_col1 );
			} else {
				GOTO_Line(1);
				Goto_Col(1);
			}
		}
	}

	make_message('');
	Temp_Ins = Insert_Mode;
	Insert_Mode = TRUE;
	searching = TRUE;

	if(  NoPrompt && Search_Mode  )
	{
		Refresh = FALSE;
	}

	Error_level = 0;

	while(  Searching  ) {
		refresh = false;
		if(  NoPRompt  ) {
			if(  Search_Mode  ) {
				make_message( 'Searching... Replaces:' + str(count) + ' <CtrlBREAK> to abort.');
			} else {
				make_message( 'Searching... Occurrences:' + str(foundnum) + ' <CtrlBREAK> to abort.');
			}
		} else {
			make_message('Searching...   <CtrlBREAK> to abort.');
		}
		WORKING;
		orig_line = c_line;
		if(  Back_Search  ) {
			if(  Restrict  ) {
sagain2:
				Restrict_Count = C_Line - Block_Line1 + 1;
				if(  restrict_count < 1  ) {
					goto search_out;
				}
				if(  (Across_Lines)  ) {
					Return_Str = Search_Str;
					RM('SRCH_ACROSS_LINE /R=' + Str(Restrict_Count)
						 + '/B=' + Str(Back_Search));
					S_Res = Return_Int;
				} else {
					S_Res = Find_Text(Search_Str, Restrict_Count, s_flags );
				}

/* check to see if the last time we went through this loop we set the at_top
	 flag.  If so, consider this find to have failed to avoid a continuous loop */
				if (at_top)
					s_res = 0;
/* set special flag if we are already at the top of the file so we don't get
	 into a continuous loop */
				at_top = ((c_line == 1) && (c_col == 1));

				if(  s_res  ) {
					if(  block_stat == 2  ) {
						if(  (c_col < block_col1) | ((c_col + length(found_str) - 1) > block_Col2)  ) {
							call searchmove;
							goto sagain2;
						}
					} else if(  block_stat == 3  ) {
						if(  ((c_line == block_line1) & (c_col < block_col1))  ) {
							call searchmove;
							goto sagain2;
						} else if(  ((c_line == block_line2) & ((c_col + length(found_str) - 1) > block_col2))  ) {
							call searchmove;
							goto sagain2;
						}
					}
				}
			} else {
				if(  (Across_Lines)  ) {
					Return_Str = Search_Str;
					RM('SRCH_ACROSS_LINE /R=' + Str(Restrict_Count)
						 + '/B=' + Str(Back_Search));
					S_Res = Return_Int;
				} else {
					S_Res = Find_Text(Search_Str, Restrict_Count, s_flags );
				}

/* check to see if the last time we went through this loop we set the at_top
	 flag.  If so, consider this find to have failed to avoid a continuous loop */
				if (at_top)
					s_res = 0;
/* set special flag if we are already at the top of the file so we don't get
	 into a continuous loop */
				at_top = ((c_line == 1) && (c_col == 1));

			}
		} else {
			if(  Restrict  ) {
sagain:
				Restrict_Count = Block_Line2 - C_Line + 1;
				if(  restrict_count < 1  ) {
					goto search_out;
				}
				if(  (Across_Lines)  ) {
					Return_Str = Search_Str;
					RM('SRCH_ACROSS_LINE /R=' + Str(Restrict_Count)
						 + '/B=' + Str(Back_Search));
					S_Res = Return_Int;
				} else {
					S_Res = Find_Text(Search_Str, Restrict_Count, s_flags );
				}
				if(  s_res  ) {
					if(  block_stat == 2  ) {
						if(  (c_col < block_col1) | ((c_col + length(found_str) - 1) > block_Col2)  ) {
							call searchmove;
							goto sagain;
						}
					} else if(  block_stat == 3  ) {
						if(  ((c_line == block_line1) & (c_col < block_col1))  ) {
							call searchmove;
							goto sagain;
						} else if(  ((c_line == block_line2) & ((c_col + length(found_str) - 1) > block_col2))  ) {
							call searchmove;
							goto sagain;
						}
					}
				}
			} else {

				if(  (Across_Lines)  ) {
					Return_Str = Search_Str;
					RM('SRCH_ACROSS_LINE /R=' + Str(Restrict_Count)
						 + '/B=' + Str(Back_Search));
					S_Res = Return_Int;
				} else {
					S_Res = Find_Text(Search_Str, Restrict_Count, s_flags );
				}
			}
		}
/* This only occurs if user pressed <CTRL-BREAK> */
		if(  Error_Level == 1015  ) {
			Make_message('Search aborted by user.');
			refresh = true;
			redraw;
			Error_level = 0;
			GOTO S_And_R_Exit;
		}
		if(  S_Res  ) {
			if(  FoundNum == 0  ) {
/* Only after the first successful find will we create a window to put our list */
				++all_windows_count;
				if (making_list) {

// Mod to handle correct placement of .TMP files
					RM("TMP_FILE_NAME /FN=SRLST.TMP");
/*
					return_str = "SRLST.TMP";
					rm("MakeUserPath");
*/
					list_file_name = return_str;
					if (switch_file(list_file_name)) {
						if (all_windows_count == 1) {
							erase_window;
						}
					} else {
						switch_window(window_count);
						Create_Window;
					}
					file_name = list_file_name;
					window_Attr = $81;
					pad_str(sparestring,screen_width," ");
					sparestring = str_ins("",sparestring,7) + "";
					format_line = sparestring;
					List_Window_Num = Cur_Window;
					Switch_Win_Id(Active_Window);
					tab_expand = true;
/*
beep;
make_message("LINE 431 " + str(list_window_num));
read_key;
*/
					put_line_to_win("\x9" + file_name + "\x9#=OCC=WIN_ID=" +
						str(window_id) + "þ=",
						all_windows_count + all_windows_foundnum,list_window_num,false);
					tab_expand = t_tab_expand;
				} else {
					Pop_Mark;
				}
			}
			++FoundNum;
			if (making_list) {
				mark_pos;
				goto_col(c_col - (Screen_width / 2) + 7);
				sparestring = Copy(Get_Line,c_col,Screen_Width - 15);
				tabs_to_spaces(sparestring);
				tab_expand = true;
/*
beep;
make_message("LINE 450 " + str(list_window_num));
read_key;
*/
				put_line_to_win(str(c_line) + '|9' + sparestring,FoundNum +
					all_windows_count + all_windows_foundnum,list_window_num,false);
				tab_expand = t_tab_expand;
				goto_Mark;
			}
			if(  NOPROMPT == FALSE  ) {
				make_message('');
			}
			if(  Search_Mode  ) {
				CALL Do_Replace_Prompt;
			} else {
				CALL Do_Search_Prompt;
			}
		} else {
			Searching = FALSE;
			if(  FoundNum == 0  ) {
				Goto_Mark;
			}
		}
	}
search_out:
	if (making_list && foundnum) {
		Switch_Window(list_Window_num);
		tab_expand = true;
		goto_line(all_windows_foundnum + all_windows_count);
		goto_col(1);
		if (search_fwd("",1)) {
			RM("DELEOL");
			text("#=" + str(all_windows_count) + "OCC=" + str(foundnum) +
				"WIN_ID=" + str(active_window) + "þ=");
		}
		tab_expand = t_tab_expand;
		switch_win_id(active_window);
	}
	if (search_all_windows) {
		all_windows_foundnum += foundnum;
		if (!s_prompt && foundnum && !making_list && !noprompt) {
// we should only get here if we have stop on first occurrence selected
			search_all_windows = false;
			goto ALL_WINDOWS_FOUND;
		}
		pop_undo;
		SWITCH_WINDOW(CUR_WINDOW + 1);
		RM( 'FINDWIN /HIDDEN=1');
		rm('SELECT_WINDOW');
		push_undo;
		active_window = window_id;
		if (window_id != first_window) {
			all_windows_global = true;
			foundnum = 0;
			goto start_search;
		}
	}

ALL_WINDOWS_FOUND:
	Set_Global_Int('MENU_RESULT',TRUE);

	if(  Search_Mode  ) {                                                 /*  ldh  */
	 /*  call Center_Line;     */                                             /*  ldh  */
	}                                                                /*  ldh  */
	if (making_list && (foundnum || all_windows_foundnum)) {
		Switch_Window(list_Window_num);

		Set_Global_Str("!SR_LST_WN","/SS=" + Global_Str("SEARCH_STR"));

		tof;
		Switch_Win_Id(Active_Window);
		make_message("");
		RM("SEARCH_LIST");

		if (Return_Int || search_all_windows) {
			Pop_Mark;
		} else {
			Goto_Mark;
		}
	}
	if (search_all_windows) {
		sparestring = 'String Found ' + STR(all_windows_foundnum) + ' Times in ' +
			str(all_windows_count) + " files.";
	} else
		sparestring = 'String Found ' + STR(FoundNum) + ' Times.';
	Make_Message(sparestring);
	Refresh = TRUE;
	Insert_Mode = Temp_Ins;
	GOTO S_And_R_Exit;


Do_Replace_Prompt:
	if(  noprompt == FALSE  ) {
		call highlight_find;
		if(  NOT(S_Prompt)  ) {
			Searching = False;
		}
		Make_Message('');
key_loop1:
		RM('UserIn^CheckEvents /M=2/G=!SREV#/#=' + str(ev_count));
		Read_Key;
		if(  (key1 == 0) & (key2 == 250)  ) {
			RM('UserIn^CheckEvents /M=1/G=!SREV#/#=' + str(ev_count));
		} else {
			RM('UserIn^CheckEvents /M=0/G=!SREV#/#=' + str(ev_count));
		}
		if(  return_int  ) {
			return_int = parse_int('/R=', return_str);
		} else {
			goto key_loop1;
		}
		make_message('');
		if(  return_int == 2  ) {
			call searchmove;
			RET;
		} else if(  return_int == 4  ) {
			noprompt = true;
			goto NON_STOP;
		} else if(  return_int == 5  ) {
			noprompt = true;
			all_files = 1;
			goto NON_STOP;
		} else if(  return_int == 6  ) {
			Searching = FALSE;
			RET;
		} else if(  return_int == 3  ) {
			all_files = -1;
			Searching = FALSE;
			RET;
		} else if(  return_int == 1  ) {
			T_Col = C_Col;
			if(  (Across_Lines)  ) {
				Return_Str = Replace_Str;
				RM('REPL_ACROSS_LINE ');
			} else {
				Replace( Replace_Str );
			}

			if(  (No_Cursor_Movement)  ) {
				Goto_Col(T_Col);
			} else {
				if(  Back_Search  ) {
					Goto_Col(T_Col);
					Left;
				} else {
					while(  NOT(at_eol) & (cur_char == '|255')  ) {
						right;
					}
					if(  At_Eol  ) {
						right;
					}
				}
			}
			++count;
			Redraw;
			RET;
		}
		goto do_replace_prompt;
	} else {
NON_STOP:
		T_Col = C_Col;
		if(  (Across_Lines)  ) {
			Return_Str = Replace_Str;
			RM('REPL_ACROSS_LINE ');
		} else {
			Replace( Replace_Str );
		}
		if(  (No_Cursor_Movement)  ) {
			Goto_Col(T_Col);
		} else {
			if (Back_Search) {
				Goto_Col(T_Col);
				Left;
			} else {
				while(  NOT(at_eol) & (cur_char == '|255')  ) {
					right;
				}
				if(  At_Eol  ) {
					right;
				}
			}
		}
		++count;
	}
	RET;

highlight_find:
	refresh = false;
	if (across_lines) {
/* This will scroll the screen to make as much of the phrase as possible
visable */
		jx = Parse_Int('/L=',Global_Str('@LINE_SEARCH_POS')) - c_line;
		if (jx > (win_y2 - win_y1))
			jx = (win_y2 - win_y1);
		goto_line(c_line + jx);
	}

	if(  c_line > orig_line  ) {
		if(  (c_line - orig_line) <= ((win_y2 - win_y1) - c_row)  ) {
			tp = c_line;
			goto_line(orig_line);
			while(  c_line != tp  ) {
				down;
			}
		} else {                                                              /*  ldh  */
			call Center_Line;                                               /*  ldh  */
		}
	} else {
		if(  (orig_line - c_line) <= ((win_y2 - win_y1) - ((win_y2 - (win_y1 + c_row))))  ) {
			tp = c_line;
			goto_line(orig_line);
			while(  c_line != tp  ) {
				up;
			}
		} else {                                                              /*  ldh  */
			call Center_Line;                                               /*  ldh  */
		}
	}

/*
	sparestring = found_str;
	expand_tabs( sparestring, hl );
	hl = svl( sparestring );
	tp = c_col;
	goto_col(c_col + hl);
	goto_col(tp);
	if(  (win_x2 - wherex) < hl  ) {
		hl = win_x2 - wherex;
	}
 */

	if (Across_Lines) {
/* This brings the cursor back to the right place after the scrolling above */
		refresh = true;
		redraw;
		for (jx;jx > 0;jx--)
			up;
		int t_wherex = wherex,
		t_wherey = wherey;

		refresh = false;
/* Hilite each individual word in the phrase */
		Jx = Parse_Int('/#=',Global_Str('@LINE_SEARCH_POS'));
		while (Jx) {
			Jm = Parse_Int('/L=',Global_Str('@LINE_SEARCH_STR' + Str(Jx)));
			Jz = Parse_Int('/C=',Global_Str('@LINE_SEARCH_STR' + Str(Jx)));
			while (C_Line > Jm) {
				--t_wherey;
				Up;
			}
			while (C_Line < Jm) {
				++t_wherey;
				Down;
			}
			hl = length(Parse_Str('/S=',Global_Str('@LINE_SEARCH_STR' + Str(Jx))));
//			goto_col(jz);
			t_wherex = win_x1 + jz - left_offset;
			if ((win_x2 - t_wherex) < hl) {
				hl = win_x2 - t_wherex;
			}

			draw_attr(t_wherex, t_wherey, h_color | $80, hl);
			--Jx;
		}
		refresh = true;
	} else {
		search_highlight = buffer_id;
		set_virtual_display;
		refresh = true;
		hl = c_line;
		tp = c_col;
		goto_line( search_end_line );
		goto_col( search_end_col );
		goto_line(hl);
		goto_col(tp);
		redraw;
		if(event_macro != "")
		{
			key1 = 0;
			key2 = 0;
			rm(event_macro);
		}
		update_virtual_display;
		reset_virtual_display;
	}
	ret;

Do_Search_Prompt:
if (Noprompt) {
	Call searchmove;
	ret;
}

	if (NOT(S_Prompt)) {
		call highlight_find;
		Make_Message('String Found ' + STR(FoundNum) + ' Times.');
		Searching = False;
	//	if (search_all_windows)
	//		RET;
		if( !global_int("~IN_REPEAT") ) {
			Read_Key;
			push_key(key1,key2);
		}
		RET;
	}
	call highlight_find;
	Make_Message('');
	RM('UserIn^CheckEvents /M=2/G=!SREV#/#=2');
key_loop2:
	Read_Key;
	if(  (key1 == 0) & (key2 == 250)  ) {
		RM('UserIn^CheckEvents /M=1/G=!SREV#/#=2');
	} else {
		RM('UserIn^CheckEvents /M=0/G=!SREV#/#=2');
		if(  return_int == 0  ) {
			return_int = 1;
			goto search_promptx;
		}
	}
	if(  return_int  ) {
		return_int = parse_int('/R=', return_str);
	} else {
		goto key_loop2;
	}
search_promptx:
	make_message('');
	if(  return_int == 2  ) {
		Searching = FALSE;
	} else {
		call searchmove;
	}
	RET;

searchmove:
		if(  Back_Search  ) {
			LEFT;
		} else {
			RIGHT;
			while(  not(at_eol) & (cur_char == '|255')  ) {
				right;
			}
			if(  At_Eol  ) {
				right;
			}
		}
		ret;

/* <<<Added 12-Apr-90 ldh ******************************************************/
Center_Line:
	New_Line_Num = C_Line;
	tp = c_col;
/* These 6 lines will center the selected line on the screen if possible */
	while(  (C_Row > ((Win_Y2 - Win_Y1) / 2))  ) {
		Up;
	}
	while(  (C_Row < ((Win_Y2 - Win_Y1) / 2))  ) {
		Down;
	}
	Goto_Line(New_Line_Num);
	goto_col(tp);
	ret;
/******************************************************************** Added>>> */

s_and_r_exit:
	search_highlight = false;
	tab_expand = t_tab_expand;
	status_row = t_status_row;
	persistent_blocks = tpb;
	RM('UserIn^CheckEvents /M=3/F=1/G=!SREV#/#=' + str(ev_count));
	Set_Global_Int('FOUND_COUNT', foundnum);
	Pop_Undo;
	SET_GLOBAL_INT('REPSEARCH',0);
	Reg_Exp_Stat = True;
	redraw;
	if(  (Across_Lines)  ) {
		Jx = Parse_Int('/#=',Global_Str('@LINE_SEARCH_POS'));
		while(  (Jx)  ) {
			Set_Global_Str('@LINE_SEARCH_STR' + Str(Jx),'');
			--Jx;
		}
		Set_Global_Str('@LINE_SEARCH_POS','');
	}
	return_int = all_files;
}

macro SEARCH_LIST trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: SEARCH_LIST

Description: Brings up the list of search occurrences created by S_AND_R

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Active_Window = Window_Id,
			t_reg_exp_stat = reg_exp_stat,
			Line_Number,
			t_tab_expand = tab_expand,
			win_id;
	str f_name[128] = "";

	tab_expand = true;

// Mod to handle correct placement of .TMP files
	RM("TMP_FILE_NAME /FN=SRLST.TMP");
/*
	return_str = "SRLST.TMP";
	rm("MakeUserPath");
*/
	error_level = 0;
	if (Switch_File(return_str)) {
			if (xpos("þ",get_line,1)) {
				down;
			}
			set_global_int("!SR_LST_LINE", -2);
			set_global_str('IPARM_1','/C=1/L=1/TP=15/W=77/QK=1/DISPMAC=SEARCH_LIST_DISP' +
											'/DISP#=3/T=Select found line to move to:/DC=1/WIN=' + str(cur_window) );
			RM('USERIN^DATA_IN /A=2/#=1/T=FOUND LIST/H=SEARCH');
			window_Attr = $81;
			if (return_Int) {
				Goto_Col(1);
				if (Val(Line_Number,Get_Word(" \x9\xFF")) == 0) {
					mark_pos;
					if (search_bwd("%\x9?+#=",0)) {
						first_word;
						f_name = get_word(" \x9");
						win_id = parse_int("WIN_ID=",get_line);
						goto_mark;
// look for window by window_id first
						if (switch_win_id(win_id)) {
							Goto_Line(Line_Number);
							Goto FOUND;
						} else if (switch_file(f_name)) {
// if not there, look for it by name
							Goto_Line(Line_Number);
							Goto FOUND;
						}
					} else
						goto_mark;
				Make_Message("Window for search list not found.");
				}
			}
	} else {
		Make_Message("No search list found.");
	}
	Switch_Win_Id(Active_Window);
FOUND:
	RM("Select_Window");
	reg_exp_stat = t_reg_exp_stat;
	tab_expand = t_tab_expand;
}

macro SEARCH_LIST_DISP trans2 {

	reg_exp_stat = true;
	if ((c_line > (global_int("!SR_LST_LINE") + 1)) ||
		(c_line < (global_int("!SR_LST_LINE") - 1))) {
		refresh = false;
		mark_pos;
		if (search_bwd("þ=$",0)) {
			first_word;
			return_str = "/DS1=Search string: " + Parse_Str("/SS=",Global_Str("!SR_LST_WN")) + "/DS2=File name:     " + get_word(" \x9") + "/DS3=Occurrences:   " + parse_str("OCC=",get_line);
		}
		goto_mark;
		refresh = true;
  } else {
    return_str = global_str("!SR_LST_LINE");
  }
	set_global_int("!SR_LST_LINE", c_line);
  set_global_str("!SR_LST_LINE", return_str);
}

macro SRCH_ACROSS_LINE TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: SRCH_ACROSS_LINE

Description: Performs Word/Phrase search

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str
					Current_Word[80],
					T_Delimits[40] = Word_Delimits,
					Punctuation[5] = ',.?;:'
					;
	int
					Restrict_Count = Parse_Int('/R=',MParm_Str),
					Back_Search = Parse_Int('/B=',MParm_Str),
					Word_Index,
					Word_Count = 0,
					Check_Col,
					ret_val = 0
					;

	Reg_Exp_Stat = False;
	Word_Delimits = ' |9|255';
	if (Global_Str('@PHRASE_SRCH_DELIM') != '') {
		Word_Delimits = Global_Str('@PHRASE_SRCH_DELIM');
	}

	if (Ignore_Case) {
		Return_Str = Caps(Return_Str);
	}
	Ret_Val = 0;

	RM("PARSE_PHRASE /M=0");
	Word_Count = Return_Int;

	if (Word_Count < 1) {
		Goto EXIT;
	}


SEARCH_AGAIN:
	Word_Index = 1;

	if (Back_Search) {
		if ((c_line == 1) && (c_col == 1)) {
// if we are at the top of the file already, get out!
			goto exit;
		}
		if (Search_Bwd(Parse_Str('/S=',Global_Str('@LINE_SEARCH_STR1')),Restrict_Count) == 0) {
			Goto EXIT;
		}
	} else {
		if (at_eof) {
// if we are at the end of the file already, get out!
			goto exit;
		}
		if (Search_Fwd(Parse_Str('/S=',Global_Str('@LINE_SEARCH_STR1')),Restrict_Count) == 0) {
			Goto EXIT;
		}
	}
	Set_Global_Str('@LINE_SEARCH_STR1','/S=' + Parse_Str('/S=',Global_Str('@LINE_SEARCH_STR1')) + '/L=' + Str(C_Line) + '/C=' + Str(C_Col));

/* Check to make sure that we found a whole word */
	Mark_Pos;
	if ((C_Col > 1) && (XPos(Cur_Char,Punctuation,1) == 0)){
		Left;
// For the first word, we'll acccept default word delimiters as leading chars
		if (Xpos(Cur_Char,T_Delimits,1) == 0) {
			Pop_Mark;
			Word_Right;
			Goto SEARCH_AGAIN;
		}
		Right;
	}
	if (word_count > 1) {
		Goto_Col(C_Col + Length(Found_Str));
		if ((At_Eol == 0) & (Xpos(Cur_Char,Word_Delimits,1) == 0)) {
			Pop_Mark;
			Goto SEARCH_AGAIN;
		}
		Goto_Mark;
		Mark_Pos;
		Word_Right;
		if (At_Eol == 0) {
			Word_Left;
		}
	}
CHECK_NEXT_WORD:
	if (Word_Index == Word_Count) {
/* If we found all the words, then...  */
		if (Word_Count == 1) {
			Goto_Col(C_Col + Length(Found_Str));
			if ((At_Eol == 0) & (Xpos(Cur_Char,T_Delimits,1) == 0)) {
				if (back_search) {
					goto_mark;
					left;
				} else {
					Pop_Mark;
				}
				Goto SEARCH_AGAIN;
			}
		}
		Set_Global_Str('@LINE_SEARCH_POS','/L=' + Str(C_Line) + '/C=' + Str(C_Col) + '/#=' + Str(Word_Count));
		Goto_Mark;
		Ret_Val = 1;
		Goto EXIT;
	}
	++Word_Index;
EOL_DELIMITS:
	Word_Right;
	if (((Xpos(Cur_Char,Word_Delimits,1) | (At_Eol == True)) > 0) & (At_Eof == False)) {
/* We would only get here if there are word delimiters at the end of a line */
		Goto EOL_DELIMITS;
	}
	Check_Col = C_Col;
	if (Word_Index == Word_Count) {
/* On the last word, allow trailing chars according to users word delimits */
		Current_Word = Get_Word(T_Delimits);
	} else {
		Current_Word = Get_Word(Word_Delimits);
	}
	if (Ignore_Case) {
		Current_Word = Caps(Current_Word);
	}
	if (Current_Word != Parse_Str('/S=',Global_Str('@LINE_SEARCH_STR' + Str(Word_Index)))) {
		Goto_Mark;
		if (Back_Search) {
			Left;
		} else {
			Right;
		}
		Goto SEARCH_AGAIN;
	}
	Set_Global_Str('@LINE_SEARCH_STR' + Str(Word_Index),'/S=' + Current_Word + '/L=' + Str(C_Line) + '/C=' + Str(Check_Col));

	Goto CHECK_NEXT_WORD;

EXIT:
	Return_Int = Ret_Val;
	Word_Delimits = T_Delimits;
}

macro PARSE_PHRASE TRANS2 {
	str
					Search_Str = Return_Str,
					Current_Word[80],
					g_str[18]
					;
	int
					Copy_Index = 1,
					Word_Count = 0
					;
	char  Ch
					;
	if (parse_int("/M=",mparm_str)) {
		g_str = '@LINE_REPLACE_STR';
	} else {
		g_str = '@LINE_SEARCH_STR';
	}

GET_SEARCH_WORD:
	Current_Word = '';
MORE_SEARCH_WORD:
	if (Copy_Index <= Svl(Search_Str)) {
		Ch = Str_Char(Search_Str,Copy_Index);
		++Copy_Index;
		if (Xpos(Ch,Word_Delimits,1) == 0) {
			Current_Word = Current_Word + Ch;
			Goto MORE_SEARCH_WORD;
		}
	}
	if (Current_Word != '') {
		++Word_Count;
		Set_Global_Str(g_str + Str(Word_Count),'/S=' + Current_Word);
	}

NEXT_SEARCH_WORD:
/* Find the beginning next word in Search_Str for next time */
	while ((Copy_Index < Svl(Search_Str)) &
					(Xpos(Str_Char(Search_Str,Copy_Index),Word_Delimits,1) > 0)) {
		++Copy_Index;
	}

	if (Copy_Index <= Svl(Search_Str)) {
		Goto GET_SEARCH_WORD;
	}

	return_int = Word_Count;
}

macro REPL_ACROSS_LINE TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: REPL_ACROSS_LINE

Description: Performs Word/Phrase replace

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str T_Delimits[40] = Word_Delimits;

	int Search_Count = Parse_Int('/#=',Global_Str('@LINE_SEARCH_POS')),
			Done_Count,
			Replace_Count,
			T_line = C_Line,
			T_Row = C_Row
			;

	refresh = false;
	Word_Delimits = ' |9|255';
	if (Global_Str('@PHRASE_SRCH_DELIM') != '') {
		Word_Delimits = Global_Str('@PHRASE_SRCH_DELIM');
	}
	RM("PARSE_PHRASE /M=1");
	Replace_Count = Return_Int;

	Mark_Pos;
/* Place markers at the beginning of every found word, then at the end of the
found phrase */
	Goto_Line(Parse_Int('/L=',Global_Str('@LINE_SEARCH_POS')));
	Goto_Col(Parse_Int('/C=',Global_Str('@LINE_SEARCH_POS')));
	Insert_Mode = True;
	Text('|0');

	for (Done_count = 1;done_count <= search_count;done_count++) {
		Goto_Line(Parse_Int('/L=',Global_Str('@LINE_SEARCH_STR' + Str(Done_Count))));
		Goto_Col( Parse_Int('/C=',Global_Str('@LINE_SEARCH_STR' + Str(Done_Count))));
		Insert_Mode = False;
		Text('|0');
	}
	Goto_Mark;
	Insert_Mode = True;

	for (Done_count = 1;done_count <= search_count;done_count++) {
		Goto_Line(Parse_Int('/L=',Global_Str('@LINE_SEARCH_STR' + Str(Done_Count))));
		Goto_Col(1);
		search_Fwd('|0',1);
		Del_Chars(Length(Parse_Str('/S=',Global_Str('@LINE_SEARCH_STR' + Str(Done_Count)))));
		if (Done_Count <= Replace_Count) {
			Text(Parse_Str('/S=',Global_Str('@LINE_REPLACE_STR' + Str(Done_Count))));
		}
	}

	while (search_count < Replace_Count) {
		++search_Count;
		Text(" " + Parse_Str('/S=',Global_Str('@LINE_REPLACE_STR' + Str(Search_Count))));
	}

/* Get rid of end of found phrase mark */
	if (Search_Fwd('|0',1)) {
		Del_Char;
	}

/* Fix it so the display does not scroll unless neccessary */
	t_row = t_row + (c_line - t_line);
	t_line = c_line;
	while (c_row < t_row) {
		down;
	}
	goto_line(t_line);
	refresh = true;

	for (replace_count;replace_count > 0;replace_count--)
		Set_Global_Str('@LINE_REPLACE_STR' + Str(Replace_Count),"");

	Word_Delimits = T_Delimits;
}

macro SrchIntr NO_BREAK trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: SrchIntr

Description:  Search (and Replace) User Interface.  This macro is called
							by S_AND_R.

Parameters:		/M=n	Mode, 0 = Search, 1 = Search and Replace
							/H=str Help String
							/SRG=str  Name of the global_str to put the search str in
							/RPG=str  Name of the global_str to put the replace str in
							/SWG=str  Name of the global_str to put the switches str in
							/SWONLY=1	Edit switches only

Returns:
						Return_Int > 0 IF START_SEARCH WAS SELECTED

							 (C) Copyright 1991 by American Cybernetics, Inc.
ÚÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄSEARCHÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ·
³Search For:                                                         ÞÝº
³                                                                       º
³Type:                               Direction:                         º
³  ( ) Literal                         () Forward  ( ) Backward        º
³  ( ) Regular expressions           Mode:                              º
³  ( ) Word/Phrase search              () Stop on first occurrence     º
³Options:                              ( ) Prompt for next search       º
³  [ ] Case sensitive                  ( ) Find all occurrences         º
³  [ ] Global search                                                    º
³  [ ] Restrict to marked block                                         º
³                                                                       º
³                   OK<ENTER>Ü Cancel<ESC>Ü Help<F1>Ü Short form<F5>Ü   º
³                    ßßßßßßßßß  ßßßßßßßßßßß  ßßßßßßßß  ßßßßßßßßßßßßßß   º
ÔÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍ¼
ÚÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄSEARCH AND REPLACEÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ·
³Search For:                                                         ÞÝº
³Replace With: ùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùùÞÝº
³                                                                       º
³Type:                               Direction:                         º
³  ( ) Literal                         () Forward  ( ) Backward        º
³  ( ) Regular expressions           Mode:                              º
³  ( ) Word/Phrase search              ( ) Replace first occurrence onlyº
³Options:                              () Prompt for each replace      º
³  [ ] Case sensitive                  ( ) Replace all occurrences      º
³  [ ] Global search                 Leave cursor at:                   º
³  [ ] Restrict to marked block        () End of replace string        º
³                                      ( ) Start of replace string      º
³                                                                       º
³                   OK<ENTER>Ü Cancel<ESC>Ü Help<F1>Ü Long form<F5>Ü    º
³                    ßßßßßßßßß  ßßßßßßßßßßß  ßßßßßßßß  ßßßßßßßßßßßßß    º
ÔÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍ¼
*******************************************************************************/
		int  search_mode, sw_pos
					, go_pos, jx,
					ic, gs, ps, regs, rb, bs, np, cs, ls, ml, aw, opt, count,
						 button_line, smode,
						 swonly = parse_int('/SWONLY=', mparm_str),
						 menu = menu_create
						 ;
		str  help_str[40], title_str[30] = 'SEARCH' ;

		help_str = Global_Str(parse_str('/SWG=', mparm_str));
		search_mode = parse_int( '/M=', mparm_str );
		if (search_mode) {
			title_str = 'SEARCH AND REPLACE';
		}
again:
		sw_pos = 2;
		ic = (xpos( 'I', help_str, 1 ) == 0);
		gs = (xpos( 'G', help_str, 1 ) != 0);
		ps = (xpos( 'P', help_str, 1 ) != 0);
		bs = (xpos( 'B', help_str, 1 ) != 0);
		regs = (xpos( 'X', help_str, 1 ) == 0);
		rb = (xpos( 'R', help_str, 1 ) != 0);
		np = (xpos( 'N', help_str, 1 ) != 0);
		cs = (xpos( 'C', help_str, 1 ) != 0);
		ls = (xpos( 'W', help_str, 1 ) != 0);
		ml = (xpos( 'L', help_str, 1 ) != 0);
		aw = (xpos( 'A', help_str, 1 ) != 0);
		if (!search_mode && np) {
			ml = 1;
		}

		if (global_int('SEARCH_INTR_STYLE')) {

			if(  swonly  ) {
				sw_pos = 1;
			} else {
				menu_set_item( menu, 1, 'Search For:  ',	Global_Str(parse_str('/SRG=', mparm_str)),
			'/L=1/QK=1/C=1/W=54/ML=254/HISTORY=SEARCH_HISTORY',0,0, 0);
				if(  Search_Mode  ) {
					++sw_pos;
					menu_set_item( menu, 2, 'Replace With:',Global_Str(parse_str('/RPG=', mparm_str)),
						'/L=2/HISTORY=REPLACE_HISTORY/QK=1/C=1/W=54/ML=254',0,0, 0);
				}
			}
			jx = sw_pos;
			menu_set_item( menu, sw_pos, 'Switches:    ', help_str,
					'/QK=2/C=1/W=20/ML=60/GO=1/L=' + str(jx),0,0, 0);
			++sw_pos;
			menu_set_item( menu, sw_pos, 'I = Ignore case','',
			'/C=3/ML=254/GO=1/L=' + str(jx + 2),10,0, 0);
			++sw_pos;
			menu_set_item( menu, sw_pos, 'X = regular eXpressions OFF','',
			'/C=3/ML=254/GO=1/L=' + str(jx + 3),10,0, 0);
			++sw_pos;
			menu_set_item( menu, sw_pos, 'W = Word/phrase search','',
			'/C=31/ML=254/GO=1/L=' + str(jx + 4),10,0, 0);
			++sw_pos;
			menu_set_item( menu, sw_pos, 'G = Global search','',
			'/C=3/ML=254/GO=1/L=' + str(jx + 5),10,0, 0);
			++sw_pos;
			menu_set_item( menu, sw_pos, 'R = Restrict to block','',
			'/C=31/ML=254/GO=1/L=' + str(jx + 2),10,0, 0);
			++sw_pos;
			menu_set_item( menu, sw_pos, 'B = search Backwards','',
			'/C=31/ML=254/GO=1/L=' + str(jx + 3),10,0, 0);
			++sw_pos;
			menu_set_item( menu, sw_pos, 'P = Prompted search','',
			'/C=3/ML=254/GO=1/L=' + str(jx + 4),10,0, 0);
			++sw_pos;
			menu_set_item( menu, sw_pos, 'N = continuous (No prompts)','',
			'/C=31/ML=254/GO=1/L=' + str(jx + 5),10,0, 0);
			++sw_pos;
			menu_set_item( menu, sw_pos, 'A = search all windows','',
			'/C=3/ML=254/GO=1/L=' + str(jx + 6),10,0, 0);
			++sw_pos;
			if (!(search_mode)) {
				menu_set_item( menu, sw_pos - 2, 'L = List all occurrences','',
				'/C=31/ML=254/GO=1/L=' + str(jx + 5),10,0, 0);
/* Not sure why, but have to put this blank field in here to get the bottom
 buttons positioned correctly */
				menu_set_item( menu, sw_pos, '','',
				'/C=31/ML=254/GO=1/L=' + str(jx + 6),10,0, 0);
				++sw_pos;
				if (Global_Str("!SR_LST_WN") != "") {
					menu_set_item( menu, sw_pos, 'List previous occurrences','',
					'/C=31/KC=<F6>/K2=64/R=12/NHT=1/L=' + str(jx + 6),11,0, 0);
					++sw_pos;
				}
			}
			menu_set_item( menu, sw_pos, 'Long form','',
			'/C=58/KC=<F5>/K2=63/R=11/NHT=1/L=' + str(jx + 8),11,0, 0);
			RETURN_INT = menu;
			RM('USERIN^Data_In /HN=1/POSG=@SRPOS@/PRE=SR/S=1/#=' + str( sw_pos ) +
						'/T=' + title_str + '/H=' + parse_str('/H=', mparm_str));
			if(  return_int  ) {
				help_str = menu_item_str( menu, jx, 2 );
				Set_Global_Str(parse_str('/SWG=', mparm_str), help_str);
				Set_Global_Str(parse_str('/SRG=', mparm_str), menu_item_str( menu, 1, 2 ));
				if(  search_mode  ) {
					Set_Global_Str(parse_str('/RPG=', mparm_str), menu_item_str( menu, 2, 2 ));
				}
				if(  return_int == 11  ) {
					set_global_int('SEARCH_INTR_STYLE', 0 );
					goto again;
				}
			}

		/*******************************************************************/

		} else {
			if(  (ls)  ) {
				opt = 3;
			} else if(  (regs)  ) {
				opt = 2;
			} else {
				opt = 1;
			}

			smode = 1;
			if (ml) {
				smode = 3;
			} else if (np) {
				smode = 3;
			} else if (ps) {
				smode = 2;
			}

			if (swonly) {
				sw_pos = 1;
			} else {
				menu_set_item( menu, 1, 'Search For:  ',Global_Str(parse_str('/SRG=', mparm_str)),
				'/L=1/QK=1/C=1/W=54/ML=254/HISTORY=SEARCH_HISTORY/GO=' + str( search_mode == 0),0,0, 0);
				if(  Search_Mode  ) {
					++sw_pos;
					menu_set_item( menu, 2, 'Replace With:',Global_Str(parse_str('/RPG=', mparm_str)),
				'/L=2/HISTORY=REPLACE_HISTORY/QK=1/C=1/W=54/ML=254/GO=1',0,0, 0);
				}
			}
			jx = sw_pos;
			menu_set_item( menu, jx, 'Type:','',
				'/C=1/L=' + str( sw_pos + 1),10,0, 0);
			++jx;
			menu_set_item( menu, jx, 'Literal                  ','',
				'/QK=1/C=2/L=' + str(sw_pos + 2),12, opt == 1, 0);
			++jx;
			menu_set_item( menu, jx, 'Regular expressions      ','',
				'/QK=9/C=2/L=' + str(sw_pos + 3),12, opt == 2, 0);
			++jx;
			menu_set_item( menu, jx, 'word/Phrase search       ','',
				'/QK=7/C=2/L=' + str(sw_pos + 4),12, opt == 3, 0);
			++jx;

			menu_set_item( menu, jx, 'Direction:','',
				'/C=34/L=' + str(sw_pos + 1),10,0, 0);
			++jx;
			menu_set_item( menu, jx, 'Forward ','',
				'/QK=1/C=35/L=' + str(sw_pos + 2),12, Not(bs), 0);
			++jx;
			Help_Str = '';
			if(  (Search_Mode)  ) {
				Help_Str = '     ';
			}
			menu_set_item( menu, jx, 'Backward    ' + Help_Str ,'',
				'/QK=1/C=48/L=' + str(sw_pos + 2),12, bs, 0);
			++jx;

			menu_set_item( menu, jx, 'Options:','',
				'/C=1/L=' + str( sw_pos + 5),10,0, 0);
			++jx;
			menu_set_item( menu, jx, 'Case sensitive           ','',
				'/QK=1/C=2/L=' + str(sw_pos + 6),13, ic, 0);
			++jx;
			menu_set_item( menu, jx, 'Global search            ','',
				'/QK=1/C=2/L=' + str(sw_pos + 7),13, gs, 0);
			++jx;
			menu_set_item( menu, jx, 'Restrict to marked block ','',
				'/QK=13/C=2/L=' + str(sw_pos + 8),13, rb, 0);
			++jx;
			menu_set_item( menu, jx, 'Search all windows       ','',
				'/QK=12/C=2/L=' + str(sw_pos + 9),13, aw, 0);
			++jx;

			button_line = 9;
			if(  search_mode  ) {
				menu_set_item( menu, jx, 'Mode:','',
				'/C=34/L=' + str( sw_pos + 3),10,0, 0);
				++jx;
				menu_set_item( menu, jx, 'Replace first occurrence only ','',
				'/C=35/QK=15/L=' + str(sw_pos + 4),12, (smode == 1), 0);
				++jx;
				menu_set_item( menu, jx, 'Prompt for each replace       ','',
				'/C=35/QK=1/L=' + str(sw_pos + 5),12,(smode == 2), 0);
				++jx;
				menu_set_item( menu, jx, 'Replace all occurrences       ','',
				'/C=35/QK=9/L=' + str(sw_pos + 6),12,(smode == 3), 0);
				++jx;
				menu_set_item( menu, jx, 'Leave cursor at:','',
				'/C=34/L=' + str(sw_pos + 7),10,0, 0);
				++jx;
				menu_set_item( menu, jx, 'End of replace string         ','',
				'/QK=2/C=35/L=' + str(sw_pos + 8),12, (cs == 0), 0);
				++jx;
				menu_set_item( menu, jx, 'Start of replace string       ','',
				'/QK=2/C=35/L=' + str(sw_pos + 9),12, (cs), 0);
				++jx;
				menu_set_item( menu, jx, 'Short form','',
				'/C=55/KC=<F5>/K2=63/R=11/NHT=1/L=' + str(sw_pos + 11),11,0, 0);
			} else {
				menu_set_item( menu, jx, 'Mode:','',
				'/C=34/L=' + str( sw_pos + 3),10,0, 0);
				++jx;
				menu_set_item( menu, jx, 'Stop on first occurrence ','',
				'/C=35/QK=3/L=' + str(sw_pos + 4),12,(smode == 1), 0);
				++jx;
				menu_set_item( menu, jx, 'Prompt for next search   ','',
				'/C=35/QK=1/L=' + str(sw_pos + 5),12,(smode == 2), 0);
				++jx;
				menu_set_item( menu, jx, 'List all occurrences     ','',
				'/C=35/QK=6/L=' + str(sw_pos + 6),12,(smode == 3), 0);
				++jx;

				if (Global_Str("!SR_LST_WN") != "") {
					menu_set_item( menu, jx, 'List previous occurrences','',
					'/C=40/KC=<F6>/K2=64/R=12/NHT=1/L=' + str(sw_pos + 8),11,0, 0);
					++jx;
				}
				menu_set_item( menu, jx, 'Short form','',
				'/C=55/KC=<F5>/K2=63/R=11/NHT=1/L=' + str(sw_pos + 11),11,0, 0);
			}
			return_int = menu;
			RM('USERIN^Data_In /HN=1/POSG=@SRPOS@/PRE=SR/S=1/#=' + str( jx ) +
						'/T=' + title_str + '/H=' + parse_str('/H=', mparm_str));
			if(  NOT(swonly) | RETURN_INT  ) {
				help_str = '';
				if(  menu_item_int( menu, sw_pos + 8, 2 ) == 0  ) {
					help_str = help_str + 'I';
				}
				if(  menu_item_int( menu, sw_pos + 2, 2 ) == 0  ) {
					help_str = help_str + 'X';
				}
				if(  menu_item_int( menu, sw_pos + 6, 2 ) != 0  ) {
					help_str = help_str + 'B';
				}
				if(  menu_item_int( menu, sw_pos + 9, 2 ) != 0  ) {
					help_str = help_str + 'G';
				}
				if(  menu_item_int( menu, sw_pos + 10, 2 ) != 0  ) {
					help_str = help_str + 'R';
				}
				if(  menu_item_int( menu, sw_pos + 11, 2 ) != 0  ) {
					help_str = help_str + 'A';
				}
				if(  menu_item_int( menu, sw_pos + 3, 2 ) != 0  ) {
					help_str = help_str + 'W';
				}
				if(  menu_item_int( menu,  sw_pos + 14 , 2 ) != 0  ) {
					help_str = help_str + 'P';
				}
				if(  menu_item_int( menu,  sw_pos + 15 , 2 ) != 0  ) {
					if (search_mode) {
						help_str = help_str + 'N';
					} else {
						help_str = help_str + 'L';
					}
				}
				if(  search_mode  ) {
					if(  menu_item_int( menu, sw_pos + 18, 2 ) != 0   ) {
						help_str = help_str + 'C';
					}
					if(  NOT(swonly)  ) {
						Set_Global_Str(parse_str('/RPG=', mparm_str), menu_item_str( menu, 2, 2 ));
					}
				}
				Set_Global_Str(parse_str('/SWG=', mparm_str), help_str);
				if(  NOT(swonly)  ) {
					Set_Global_Str(parse_str('/SRG=', mparm_str), menu_item_str( menu, 1, 2 ));
				}
				if(  return_int == 11  ) {
					set_global_int('SEARCH_INTR_STYLE', 1 );
					goto again;
				}
			}
		}
		menu_delete( menu );
}

macro SEARCH trans2  {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	SEARCH

Description:	Calls S_and_R in search only mode.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	SET_GLOBAL_INT('SEARCH_MODE',0);
	SET_GLOBAL_INT('REPSEARCH',0);
	RM( 'S_AND_R '+mparm_str);
}

macro S_REPL trans2  {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	S_REPL

Description:	Calls S_and_R in search and replace mode.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	SET_GLOBAL_INT('SEARCH_MODE',1);
	SET_GLOBAL_INT('REPSEARCH',0);
	RM( 'S_AND_R '+mparm_str );
}

macro REPSRCH trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	S_REPL

Description:	Calls S_and_R in repeat mode.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Tint, tpb = persistent_blocks;
	persistent_blocks = TRUE;
	push_undo;
	Mark_Pos;
	if(  (Global_Int('SEARCH_MODE'))  ) {
		TInt = XPOS('B',Caps(Global_Str('REPL_SWITCHES')),1);
	} else {
		TInt = XPOS('B',Caps(Global_Str('SWITCHES')),1);
	}

	if(  (TInt)  ) {
		Left;
	} else {
		Right;
		while(  NOT(at_eol) & (cur_char == '|255')  ) {
			right;
		}
	}
	if(  (Global_Str('SEARCH_STR') != '')  ) {
		SET_GLOBAL_INT('REPSEARCH',1);
	}
	RM( 'S_AND_R /NG=1' + mparm_str );

	if(  (return_int > 0) & (global_int('FOUND_COUNT') == 0)  ) {
		Goto_Mark;
		Make_Message('No more occurrences.');
	} else {
		Pop_Mark;
	}
	persistent_blocks = tpb;
	pop_undo;
}


int FILESRCH( str ppath[128], mask[13], &root,
							int search_dirs, sm, flags, sub ) TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:  FILESRCH

Description:  Called by FS to perform the actual multi-file search.

Parameters:		Return_Str = Search_Str
							path    Path(s) to search
							search_dirs        1 = search subdirectories.
							root         Root path to search if no dir is specified in the
													 paths
							sm           Search files in Memory.  If a file is already loaded,
													 then search there instead.
							flags     Search flags

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	str  t_search_rec[80] = file_search_rec,
			 path[128]
			 ;

	int  t_attr = file_search_attr,
					 doserror,
					 t_undo_stat,
					 tw = cur_window,
					 jx,
					 pp
					 ;

	if(  sub == 0  ) {
		t_undo_stat = undo_stat;
		refresh = false;
		undo_stat = false;
		put_box( 4,5,75,20, 0, m_b_color, 'SEARCHING FILES', true );
		write('Abort', 35, 19, 0, button_color );
		write('<ESC>', 40, 19, 0, button_key_color );
		Write('Searching for:', 5, 6, 0, m_t_color );
		Write( copy(return_str,1, 51), 21, 6, 0, m_s_color );
		write('In:', 5, 7, 0, m_t_color );
		draw_char( 196, 5, 8, m_t_color, 68 );
		text_color_vp = m_s_color;
		set_vp( 5, 9, 72, 18 );
		gotoxy_vp( 1, 1 );
		return_int = 0;

		pp = 1;

	 parse_loop:
		path = ppath;
		if(  pp < svl(path)  ) {
			jx = xpos( ' ', path, pp );
			if(  jx == 0  ) {
				jx = svl( path ) + 1;
			}
			path = remove_space( copy( path, pp, jx - pp ) );
			pp = jx + 1;
			mask = truncate_path(path);
			path = get_path(path);
			if(  path == ''  ) {
				path = root;
			}
			path = fexpand( path );
			if(  (copy( path, svl(path), 1) != '\')  ) {
				path = path + '\';
			}
			call do_search;
			goto parse_loop;
		}
	} else {
		path = ppath;
		call do_search;
	}

	goto exit;


do_search:
	File_Search_Attr = $37;

	if(  search_dirs  ) {
		doserror = first_file( path + '*.*' );
		while(  doserror == 0  ) {
			call check_abort;
			if(  return_int == -1  ) {
				goto search_exit;
			}
			if(  ((last_file_attr & $10) != 0) & (last_file_name != '.')
					& (last_file_name != '..')  ) {

					if( filesrch( path + last_file_name + '\', mask, root, 1, sm, flags, 1 ) == - 1)
					{
						goto search_exit;
					}

			}
			doserror = next_file;
		}
	}
	Write_Vp( '|13|10' + path );
	draw_char( 32, 10, 7, m_t_color, 60 );
	write( path + mask, 10, 7, 0, m_s_color );

	File_Search_Attr = $27;
	doserror = first_file( path + mask );
	if(  doserror == 0  ) {
			search_loop:
				call check_abort;
				if(  return_int == -1  ) {
					goto search_exit;
				}
				error_level = 0;
				write_vp( '|13|10      ' + last_file_name );

					// Now we are going to do the Search in Memory mode
				if( sm ) {
					if( switch_file( path + last_file_name )) {
						write_vp( '     ...in memory' );
						jx = 0;
						mark_pos;
						tof;
						if(find_text( return_str, 0, flags ) ) {
							jx = c_line;
						}
						goto_mark;
						switch_window( tw );
						if( jx > 0 )
								call put_success;
						if( error_level == 1010 )
								goto search_exit;
						goto next_file;
					}
				}
				jx = search_file( path + last_file_name, return_str, flags );
				if(  jx > 0  ) {
					call put_success;
				} else {
					if(  error_level == 1010  ) {
						goto SEARCH_EXIT;
					}
				}
		 next_file:
				doserror = next_file;
				if(  doserror == 0  ) {
					goto search_loop;
				}
		}
SEARCH_EXIT:
	RET;


put_success: {
		write_vp( '|13|16' );
		goto_col(2);
		text( last_file_name );
		goto_col( 15 );
		text( 'L:' + str(jx ) );
		goto_col( 27 );
		text( path );
		down;
		ret;
	}

CHECK_ABORT:
	if(  CHECK_KEY  ) {
		if(  (key1 == 0) & (key2 == 250)  ) {
			if(  (mou_last_y == 19) & (mou_last_x > 34) & (mou_last_x < 46)  ) {
			do_abort:
				write('Abort<ESC>', 35, 19, 0, m_h_color );
				return_int = -1;
			}
		} else if(  (key1 == 27)  ) {
			goto do_abort;
		}
	}
	RET;

EXIT:
	if(  sub == 0  ) {
		kill_box;
		set_vp( 1, 1, screen_width, screen_length );
		undo_stat = t_undo_stat;
	}

	file_search_attr = t_attr;
	file_search_rec = t_search_rec;
	return(return_int);
} /* FILESRCH */


macro FS TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: fs

Description:  Multi-File search.  Will prompt for a filespec (wild cards are
							allowed), a search string	and search switches, and (using
							MEFIND.EXE) perform a search through all the files matching the
							filespec, and then put up a list of all the matching files.  The
							user may then select one of the files, and the cursor will
							automatically go the the line where the match was found.

Parameters:		/N=		Skips directly to the filename list, using the last
										set of files found by MEFIND.
							/M=  If > 0 will do search and replace


							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
		str  search_str[80], switches_str[10], filespec[80], sstr[80],
					mode_str[15] = '',replace_str[80],
					help_str[15] = 'FILESEARCH', root[90];
		int  t_win, file_count, str_count, nl, nc, jx, x1, y1, menu_count,
					search_mode = (parse_int('/M=',mparm_str) > 0), menu = menu_create,
					list_window , already_loaded, leave_files_open, flags = 0,
					all_files = 0;
		int  search_dirs, use_reg_exp, case_sensitive, search_in_mem, result,
				replace_window = 0,
				dialog_start = global_int("!FS_DIALOG_START_" + str(search_mode));
		str  tc[1] , Find_Name[80];

		if (dialog_start < 1) {
			dialog_start = 1;
		}
		t_win = window_id;
		refresh = false;
		mark_pos;
		if(  (c_line == block_line1) &
				(block_stat > 0) & (block_line1 == block_line2)  ) {
			if(  (block_stat == 1)  ) {
				first_word;
				set_global_str('SEARCH_HISTORY0', get_word('') );
			} else {
				set_global_str('SEARCH_HISTORY0', copy(get_line,block_col1, (block_col2 - block_col1) + 1));
			}
/*
			} else if(  (block_stat == 2)  ) {
				set_global_str('SEARCH_HISTORY0', copy(get_line,block_col1, (block_col2 - block_col1) + 1));
			} else if(  (block_stat == 3)  ) {
				set_global_str('SEARCH_HISTORY0', copy(get_line,block_col1, block_col2 - block_col1));
			}
*/
		} else {
			right;
			word_left;
			set_global_str('SEARCH_HISTORY0', get_word(word_delimits) );
		}
		goto_mark;

		result = 0;
		set_global_int('MENU_LEVEL', global_int('MENU_LEVEL') + 1 );

		if (search_mode) {
			help_str = 'FILESRCHREPL';
			switches_str = caps(Global_Str('FRSWITCHES'));
		} else
			switches_str = caps(Global_Str('FSWITCHES'));

//		use_reg_exp = xpos('R',switches_str, 1) != 0;
//		case_sensitive = xpos('C',switches_str, 1) != 0;

		use_reg_exp = xpos('X',switches_str, 1) == 0;
		case_sensitive = xpos('I',switches_str, 1) == 0;

		search_dirs = xpos('S',switches_str, 1) != 0;
		search_in_mem = xpos('F',switches_str, 1) != 0;
		leave_files_open = xpos('O',switches_str, 1) != 0;
		if (search_mode) {
			switch_window( window_count );
			create_window;
			replace_window = window_id;
			switch_win_id(t_win);
		}


// Mod to handle correct placement of .TMP files
		RM("TMP_FILE_NAME /FN=MEFIND.TMP");
/*
		return_str = "MEFIND.TMP";
		rm("MakeUserPath");
*/
		find_name = Return_Str;

		if(  xpos('/N', caps(mparm_str), 1 )  ) {
			if(  NOT(switch_file(Find_Name))  ) {
				switch_window( window_count );
				create_window;
				window_attr = $80;
				load_file(find_name);
				if(  error_level == 0  ) {
					goto load_find_file;
				}
				file_name = find_name;
				rm('MEERROR^MessageBox /M=No matched file available.');
			} else {
				goto load_find_file;
			}
		}

		search_str = Global_Str('SEARCH_STR');
		replace_str = Global_Str('REPLACE_STR');

		menu_set_item(menu,2,'Search For:  ',search_str,
			'/QK=1/C=1/W=40/ML=254/HISTORY=SEARCH_HISTORY/L=2',0,0,0);
		menu_count = 3;
		if (search_mode) {
			mode_str = ' AND REPLACE';
			menu_set_item(menu,3,'Replace with:',replace_str,
				'/QK=1/C=1/W=40/ML=254/HISTORY=REPLACE_HISTORY/L=3',0,0,0);
			++menu_count;
			filespec = Global_Str('FRFILESPEC');
		} else {
			filespec = Global_Str('FFILESPEC');
		}

		menu_set_item(menu,1,'Filespec:    ',filespec,
			'/QK=1/C=1/W=40/ML=80/HISTORY=FILESPEC_HISTORY/L=1',0,0,0);
		menu_set_item(menu,menu_count,'Options:','',
			'/C=1/L=' + str(++menu_count),10,0,0);
		menu_set_item(menu,menu_count,'Search sub-directories  ','',
			'/QK=12/C=2/W=1/L=' + str(++menu_count),13,search_dirs,0);
		menu_set_item(menu,menu_count,'Case Sensitive          ','',
			'/QK=1/C=2/W=1/L=' + str(++menu_count),13,case_sensitive,0);
		menu_set_item(menu,menu_count,'Regular Expressions     ','',
			'/QK=9/C=2/W=1/L=' + str(++menu_count),13,use_reg_exp,0);
		menu_set_item(menu,menu_count,'search files in Memory  ','',
			'/QK=17/C=2/W=1/L=' + str(++menu_count),13,search_in_mem,0);
		if (search_mode) {
			menu_set_item(menu,menu_count,'Leave all files open    ','',
				'/QK=1/C=2/W=1/L=' + str(++menu_count),13,leave_files_open,0);
			menu_set_item(menu,menu_count,'Starting path:',global_str('FSEARCH_REPL_PATH'),
				'/QK=10/C=1/W=37/ML=80/L=' + str(menu_count + 2),0,0,0);
		} else
			menu_set_item(menu,menu_count,'Starting path:',global_str('FSEARCH_PATH'),
				'/QK=10/C=1/W=37/ML=80/L=' + str(menu_count + 2),0,0,0);

		return_int = menu;
		RM('UserIn^Data_In /HN=1/POSG=@FSPOS@/H=' + help_str +
			'/S=' + str(dialog_start) + '/#=' + str(menu_count) +
			'/T=MULTIPLE FILE SEARCH' + mode_str);
		set_global_str('SEARCH_HISTORY0', '' );
		filespec = menu_item_str(menu,1,2);
		search_str = menu_item_str(menu,2,2);
		if (search_mode) {
			replace_str = menu_item_str(menu,3,2);
			set_Global_Str('REPLACE_STR', replace_str);
			set_Global_Str('FRFILESPEC', filespec);
		} else {
			set_Global_Str('FFILESPEC', filespec);
		}
		SET_GLOBAL_STR('FLAST_CHOICE', '');
		set_Global_Str('SEARCH_STR', search_str);
		set_Global_Str('FSEARCH_STR', search_str);

		search_dirs = menu_item_int(menu,4 + search_mode,2);
		case_sensitive = menu_item_int(menu,5 + search_mode,2);
		use_reg_exp = menu_item_int(menu,6 + search_mode,2);
		search_in_mem = menu_item_int(menu,7 + search_mode,2);
		if (search_mode) {
			leave_files_open = menu_item_int(menu,8 + search_mode,2);
			set_global_str('FSEARCH_REPL_PATH',menu_item_str(menu,8 + (search_mode * 2),2));
		} else
			set_global_str('FSEARCH_PATH',menu_item_str(menu,8 + (search_mode * 2),2));

		switches_str = '';
		reg_exp_stat = use_reg_exp;
		ignore_case = ( case_sensitive == 0 );

		if (!reg_exp_stat) {
			switches_str = 'X';
		}
		if (ignore_case) {
			switches_str = switches_str + 'I';
		}

		if(reg_exp_stat)
		{
			if( reg_exp_style )
				flags |= _RegExp;
			else
				flags |= _OldExp;
		}
		if(!ignore_case)
			flags |= _CaseSensitive;

/*
		if(  use_reg_exp  ) {
			switches_str = 'R';
		}
		if(  case_sensitive  ) {
			switches_str = switches_str + 'C';
		}
*/
		if(  search_dirs  ) {
			switches_str = switches_str + 'S';
		}
		if(  search_in_mem  ) {
			switches_str = switches_str + 'F';
		}
		if(  leave_files_open  ) {
			switches_str = switches_str + 'O';
		}
/*
		if (search_mode)
			set_Global_Str('FRSWITCHES',switches_str);
		else
			set_Global_Str('FSWITCHES', switches_str);
*/
		if(  return_int  ) {
			working;
			y1 = 5;
			if(  switch_file(Find_Name)  ) {
				erase_window;
			} else {
				switch_window( window_count );
				create_window;
				window_attr = $80;
			}
			file_name = find_name;
			return_str = search_str;
/*
beep;
make_message("REG_EXP_STAT=" + str(reg_exp_stat) + " IGNORE_CASE=" + str(ignore_case));
read_key;
*/
			if (search_mode)
			{
				root = global_str('FSEARCH_REPL_PATH');
			}
			else
			{
				root = global_str('FSEARCH_PATH');
			}
			filesrch( filespec, "",root, search_dirs, search_in_mem, flags, 0 );

			set_Global_Str('SEARCH_STR', search_str);
			if (search_mode) {
				set_global_str("FRSWITCHES","P" + switches_str);
				SET_GLOBAL_INT('SEARCH_MODE',1);
			} else {
				if(  (xpos( 'P', global_str('SWITCHES'), 1 ))  ) {
					sstr = sstr + 'P';
				} else if(  (xpos( 'N', global_str('SWITCHES'), 1 ))  ) {
					sstr = sstr + 'N';
				}
				set_global_str("FSWITCHES",switches_str);
				SET_GLOBAL_INT('SEARCH_MODE',0);
			}


			if(  (error_level != 0)  ) {
				goto exitx;
			}
			if(  (return_int == -1)  ) {
				sstr = 'Multi-file search aborted by user.';
				rm('MEERROR^MessageBox /B=2/T=Multi-File Search Error/M=' + sstr );
				goto exitx;
			}
			tof;
	 load_find_file:
	 no_create:
			window_attr = window_attr | $81;  /* make it hidden and invisible */
			if(  at_eof  ) {
				eof;
				goto_col(1);
				if(  at_eof  ) {
					RM('MEERROR^MESSAGEBOX /B=2/T=NO MATCHED FILES FOUND');
					goto exitx;
				}
			}
			if(  file_changed  ) {
				save_file;
			}
			goto_col(1);
			list_window = window_id;
			if (!Search_Mode) {
        set_global_str('IPARM_1','/C=1/TP=15/L=1/T=Select file to edit:/DC=1/INCO=-1/WIN=' + str(cur_window));
								RM('USERIN^DATA_IN /A=2/POSG=@MFSPOS@/#=1/T=MATCHED FILES/H=SEARCHLIST');
				window_attr = window_attr | $81;  /* make it hidden and invisible */
				if(  return_int != 1  ) {
					switch_win_id(t_win);
					goto exit;
				}
			}

REPLACE_LOAD:
			already_loaded = 0;
			result = 1;
			Goto_Col(1);
			jx = insert_mode;
			insert_mode = FALSE;
			text('|175');
			insert_mode = jx;
			file_changed = false;
			return_str = shorten_str(get_word(' '));
			forward_till(':');
			right;
			sstr = get_word(' ');
			val(nl,sstr);
			set_global_int('@FLAST_CHOICE', c_line);
			forward_till_not(' ');
			return_str = fexpand( get_word('') + return_str);
			if(  xpos('|254', return_str,1) != 0  ) {
				goto exitx;
			}
			if(  switch_file( return_str )  ) {
				already_loaded = 1;
				rm("Select_Window");
        make_message( file_name + ' already loaded.');
				tof;
				goto no_load;
			}
			if (search_mode) {
				switch_win_id( replace_window );
				RM('LdFiles /CW=' + str(leave_files_open));
			} else {
				switch_win_id( t_win );
				RM('LdFiles /CW=2');
			}

			if(  error_level == 0  ) {
				Make_Message('"' + File_Name + '" loaded.');
no_load:
				if (leave_files_open)
					t_win = window_id;

				down; down; down; down;
				goto_line( nl );
				if(  format_stat  ) {
					up;
				}
				goto_col( 1 );
				left;
//				reg_exp_stat = XPos('R',switches_str,1);
//				ignore_case = (XPos('C',switches_str,1) == 0);
				reg_exp_stat = (XPos('X',switches_str,1) == 0);
				ignore_case = XPos('I',switches_str,1);
				if(!XPos('X',switches_str,1))
				{
					if( reg_exp_style )
						flags |= _RegExp;
					else
						flags |= _OldExp;
				}
				if(!XPos('I',switches_str,1))
					flags |= _CaseSensitive;

				if (search_mode) {
					SET_GLOBAL_INT('REPSEARCH',1);
					RM( 'S_AND_R /FR=1/NG=1');
/* Return_Int can have 3 values:
		0 = Normal s/r occurred
		1 = Non-Stop all files
	 -1 = Abort remaining files
*/
					new_screen;
					if ((return_int == 1) && (!all_files)) {
						all_files = 1;
						set_global_str('FRSWITCHES',global_str('FRSWITCHES') + "N");
					}
					if (!already_loaded) {
						if (file_changed) {
							save_file;
						}
					}
					switch_win_id(list_window);
					down;
					goto_col(1);
					if ((!at_eof) && (return_int != -1)) {
						goto REPLACE_LOAD;
					}
					make_message('');
					switch_win_id(t_win);
				} else
				{
					find_text(global_str('FSearch_Str'),0, flags);
				}
				reg_exp_stat = true;
			} else {
				switch_win_id(t_win);
			}
		}
exit:
	if(  error_level != 0  ) {
exitx:
		switch_win_id(t_win);
	}
	t_win = window_id;
	if (switch_win_id(replace_window)) {
		delete_window;
		switch_win_id(t_win);
	}
	menu_delete(menu);
	set_global_int('MENU_LEVEL', global_int('MENU_LEVEL') - 1 );
	return_int = result;
	update_status_line;
}



macro UNDBLK TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	UNDBLK

Description:	Undents the current marked block starting and ending
							at the top and bottom lines of the marked block.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int jx,j1, EndLine = Block_Line2,
			t_block_stat = block_stat,
			t_refresh = refresh;

	if ( Block_Stat )
	{
		Working;
		Push_Undo;
		block_end;
		if(( block_stat == 3) && (block_col2 < 1))
			--EndLine;
		Refresh = False;
		Mark_Pos;
		Goto_Line(Block_Line1);
		while ( C_Line <= EndLine )
		{
			First_Word;
			J1 = C_Col;
			Tab_Left;
			if ( AT_EOL == FALSE )
					Del_Chars(j1 - C_Col);
			DOWN;
		}

		Goto_Mark;
		block_stat = t_block_stat;
		Pop_Undo;
	}
	refresh = t_refresh;
	redraw;
}

macro INDBLK TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	INDBLK

Description:	Indents the current marked block starting and ending
							at the top and bottom lines of the marked block.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Temp_Ins = Insert_Mode, EndLine = Block_Line2,
			t_block_stat = block_stat,
			t_refresh = refresh;

	if ( Block_Stat )
	{
		Push_Undo;
		block_end;
		if(( block_stat == 3) && (block_col2 < 1))
				--EndLine;
		Insert_Mode = True;
		Refresh = False;
		Mark_Pos;
		Goto_Line(Block_Line1);
		while ( C_Line <= EndLine )
		{
			// First_Word;
			goto_col(1);
			if ( !At_Eol )
			{
				Tab_Right;
			}
			DOWN;
		}

		Insert_Mode = Temp_Ins;
		Goto_Mark;
		block_stat = t_block_stat;
		Pop_Undo;
	}
	refresh = t_refresh;
	redraw;
}

macro MarkBlck trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	MarkBlck

Description:	Starts and stops line oriented block marking.

Parameters:  /O=n  If non-zero does not turn block off if existing
									 block is not a line oriented block.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int BlockLN1 = Block_Line1, BlockLN2 = Block_Line2;

	if ( Marking )
	{
		if ( Parse_Int("/O=", MParm_Str) )
		{
			if ( LINBLOCK == Block_Stat )
			{
				Set_Global_Int("!SmartBlock", 0);
				Block_Off;
				Make_Message('Block turned off.');
			}
			else
			{
				Refresh = false;
				Block_Off;
				if ( C_Line != BlockLN1 )
				{
					Goto_Line(BlockLN1);
					Block_Begin;
					Goto_Line(BlockLN2);
				}
				else
				{
					Goto_Line(BlockLN2);
					Block_Begin;
					Goto_Line(BlockLN1);
				}
				Redraw;
			}
		}
		else
		{
			Set_Global_Int("!SmartBlock", 0);
			BLOCK_END;
			Make_Message('Block marked.');
		}
	}
	else
	{
		Set_Global_Int("!SmartBlock", 0);
		BLOCK_BEGIN;
		Make_Message('Block marking on.  Press ' + Global_Str('!BM_KEY15') + ' to stop.');
	}
}

macro MColBlck trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	MColBlck

Description:	Starts and stops column oriented block marking.

Parameters:  /O=n  If non-zero does not turn block off if existing
									 block is not a column oriented block.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int BlockLN1 = Block_Line1, BlockLN2 = Block_Line2, BlockCL1,
			BlockCL2 = C_Col;

	if ( Marking )
	{
		if ( Parse_int("/O=", MParm_Str) )
		{
			if ( COLBLOCK == Block_Stat )
			{
				Set_Global_Int("!SmartBlock", 0);
				Block_Off;
				Make_Message('Block turned off.');
			}
			else
			{
				Refresh = False;
				Block_Off;
				if ( 0 != (BlockCL1 = Global_Int("!SmartBlock")) )
					Goto_Col(BlockCL1);
				else
					Set_Global_Int("!SmartBlock", C_Col);
				if ( BlockLN1 != C_Line )
				{
					Goto_Line(BlockLN1);
					Col_Block_Begin;
					Goto_Line(BlockLN2);
				}
				else
				{
					Goto_Line(BlockLN2);
					Col_Block_Begin;
					Goto_Line(BlockLN1);
				}
				Goto_Col(BlockCL2);
				Refresh = True;
				Redraw;
			}
		}
		else
		{
			BLOCK_END;
			Set_Global_Int("!SmartBlock", 0);
			Make_Message('Block marked.');
		}
	}
	else
	{
		Set_Global_Int("!SmartBlock", C_Col);
		COL_BLOCK_BEGIN;
		Make_Message('Columnar block marking on.  Press ' + Global_Str('!BM_KEY15') + ' to stop.');
	}
}

macro MStrBlck trans2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	MStrBlck

Description:	Starts and stops stream oriented block marking.

Parameters:  /O=n  If non-zero does not turn block off if existing
									 block is not a stream oriented block.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int BlockLN1 = Block_Line1, BlockLN2 = Block_Line2, BlockCL1,
			BlockCL2 = C_Col;

	if ( Marking )
	{
		if ( Parse_int("/O=", MParm_Str) )
		{
			if ( STRBLOCK == Block_Stat )
			{
				Set_Global_Int("!SmartBlock", 0);
				Block_Off;
				Make_Message('Block turned off.');
			}
			else
			{
				Refresh = False;
				Block_Off;
				if ( 0 != (BlockCL1 = Global_Int("!SmartBlock")) )
					Goto_Col(BlockCL1);
				else
					Set_Global_Int("!SmartBlock", C_Col);
				if ( BlockLN1 != C_Line )
				{
					Goto_Line(BlockLN1);
					Str_Block_Begin;
					Goto_Line(BlockLN2);
				}
				else
				{
					Goto_Line(BlockLN2);
					Str_Block_Begin;
					Goto_Line(BlockLN1);
				}
				Goto_Col(BlockCL2);
				Refresh = True;
				Redraw;
			}
		}
		else
		{
			BLOCK_END;
			Set_Global_Int("!SmartBlock", 0);
			Make_Message('Block marked.');
		}
	}
	else
	{
		Set_Global_Int("!SmartBlock", C_Col);
		STR_BLOCK_BEGIN;
		Make_Message('Stream block marking on.  Press ' + Global_Str('!BM_KEY15') + ' to stop.');
	}
}

macro BLOCKOFF TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	BLOCKOFF

Description:	Turns the current block marking off.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	if(  BLOCK_STAT > 0  ) {
		Block_Off;
		Make_Message('Block turned off.');
	} else {
		Make_Message('No Block Marked.');
	}
}

macro BLOCKOP TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	BLOCKOP

Description:	Does a block copy or move and then, if hard_cr is being used,
							does a reformat on the area under the block.

Parameters:		/BT=nn		nn is the operation type.
												0 = Block copy
												1 = Block Move
												2 = Block Delete
												3 = Inter-Window Copy
												4 = Inter-Window Move
												5 = Toggle persistent blocks

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  old_refresh, bt, bl, tb ;

	bt = parse_int( '/BT=', mparm_str );
	tb = block_stat;

	bl = block_line1;

	if( bt == 5 ) {
		persistent_blocks = (persistent_blocks == 0);
		goto exit;
	}
	if(  bt == 3  ) {
		RM( 'WINDOW^COPYBL' );
		tb = block_stat;
	}
	if(  bt == 4  ) {
		RM( 'WINDOW^MOVEBL' );
		tb = block_stat;
	}

	if(  block_stat == 0  ) {
		make_message('No block marked.');
		goto exit;
	}

	if(  bt == 0  ) {
		Copy_Block;
		Make_Message('Block copied.');
	}
	if(  bt == 1  ) {
		Move_Block;
		Make_Message('Block moved.');
	}
	if(  bt == 2  ) {
		Delete_Block;
		Make_Message('Block deleted.');
	}


	Old_refresh = refresh;
	Refresh = false;
	if(  (tb != 0) & (Hard_Cr != '|0') & (Hard_Cr != '')  ) {
		Mark_Pos;
		PUSH_UNDO;
		goto_line( bl );
		while(  C_Line <= Block_Line2  ) {
			RM( 'TEXT^REFORMAT /NR' );
		}

		Goto_Mark;
		POP_UNDO;
	}
exit:
	refresh = old_refresh;
}

#define _BUFFER_NAME getuserpath( false ) + 'BUFFER.' + Str(Buf_Num)

/****************** Multi-Edit INTEGER Macro Function ***********************

 NAME:         ClipEventProc()

 DESCRIPTION:  Event processor for the buffer/clipboard window
							 Called only by the ME kernel.

 PARAMETERS:   None

 RETURNS:      0

*****************************07-08-93 02:00pm*******************************/
int ClipEventProc()
{
	int  t_persistent_blocks = persistent_blocks;
	int  tw = window_id;

	persistent_blocks = TRUE;
	pass_event_through;
	persistent_blocks = t_persistent_blocks;
	if(window_id == tw)
	{
		window_name = 'BUFFER';
		file_changed = false;
	}
	else
	{
		int tr = refresh,
				tw2 = cur_window;

		refresh = false;
		set_global_int('!CLIP_OLD_WIN', 0);
		set_global_int('!CLIP_SHOW', 0 );
		if( switch_win_id( tw ))
		{
			window_attr = 0x81;
			switch_window(tw2);
		}
		else
		{
			set_global_int("!BUFFER_ACTIVE", 0 );
		}
		refresh = tr;
	}

	return(0);

}

/******************** Multi-Edit VOID Macro Function ************************

 NAME:         ShowClipboard

 DESCRIPTION:  Display clipboard in a window

 PARAMETERS:   buf_name is the buffer number to display

*****************************07-08-93 02:01pm*******************************/
void ShowClipboard( int buf_num = parse_int('/B=', mparm_str))
{
	int twin = window_id;

	if ( buf_num > 999 )
		buf_num = 999;
	if(  buf_num < 0  )
		buf_num = 0;
	if(switch_file( _BUFFER_NAME  ))
	{
		if(window_id != twin)
		{
			set_global_int('!CLIP_SHOW', 1 );
			set_global_int('!CLIP_OLD_WIN', twin );
			event_macro  = 'ClipEventProc';
			window_name = 'BUFFER';
			window_attr = 0x80;
			redraw;
		}
		else
		{
			event_macro  = '';
			window_attr = 0x81;
			switch_win_id(global_int('!CLIP_OLD_WIN'));
			rm("FINDWIN");
			set_global_int('!CLIP_OLD_WIN', 0 );
			set_global_int('!CLIP_SHOW', 0 );
		}

	}
}

macro CUT TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: CUT

Description:  Copies, Moves or Appends text into a hidden buffer.

Parameters:  /M    Move text (else copy text)
						 /A    Append text (else erase old buffer);
						 /B=n  n = the buffer # (0 is the default);
						 /E    Erase buffer, perform no move or copy
						 /O=n  If non-zero then the marked block will be turned
									 turned off after the operation

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  buf_num, temp_refresh, source_win, erase_buf, Append_buf, t_block_stat ;
	str  buf_name, msg = 'Block ';
	int	 tc1,tc2, tl1,tl2, ts, tundo = undo_stat;

	temp_refresh = refresh;
	push_undo;
	refresh = false;
	source_win = cur_window;
	error_level = 0;
	t_block_stat = block_stat;
	if ( !Block_Stat )
		{
			Block_Begin;
			msg = 'Line ';
		}
	block_end;
	erase_buf = XPOS('/E', MParm_Str, 1);
	append_buf = XPOS('/A', MParm_Str, 1);
	buf_num = parse_int('/B=', Mparm_Str);
	if ( buf_num > 999 )
		buf_num = 999;
	if(  buf_num < 0  )
		buf_num = 0;

	buf_name = _BUFFER_NAME;
	if ( switch_file(buf_name) == 0 )
	{
		switch_window(window_count);
		create_window;
		if(  error_level != 0  )
			goto exit;
		window_attr = $81;
		file_name = buf_name;
		// rm('MeSys^SetWindowNames');
	}
	else
	{
		// window_attr = $81;
		if(erase_buf)
		{
			undo_stat = false;
			erase_window;
			undo_stat = tundo;
			file_name = buf_name;
		}
	}

	if(!erase_buf)
	{
		set_global_int("!BUFFER_ACTIVE", 1 );
	}
	else
	{
		set_global_int("!BUFFER_ACTIVE", 0 );
	}
	ts = block_stat;
	tc1 = block_col1;
	tc2 = block_col2;
	tl1 = block_line1;
	tl2 = block_line2;

	if(  append_buf && ts  )
	{
		goto_line(tl2 + 1);

	}
	else
	{
		eof;
		if(  (c_col != 1)  )
		{
			down;
			goto_col(1);
		}
	}

	 /* TMJ - 07-07-93 01:16pm
		if(  (append_buf == 0) | (erase_buf)  )
		{
			Erase_Window;
			file_name = buf_name;
		}
		else
		{
			EOF;
			if(  (c_col != 1)  )
			{
				down;
				goto_col(1);
			}
		}

		*/

	if(  erase_buf == 0  )
	{
//		if(  t_block_stat == 0  )
//		{
//			make_message('No block marked.');
//			goto exit;
//    }
		if(  XPos('/M', MParm_Str,1) == 0  )
		{
			Window_Copy(source_win);
			if(  append_buf  )
				Make_Message(msg+'appended to buffer ' + Str(buf_num));
			else
				Make_Message(msg+'copied to buffer ' + Str(buf_num));
		}
		else
		{
			Window_Move(source_win);
			Make_Message(msg+'moved to buffer ' + Str(buf_num));
		}
		if(  append_buf  )
		{
			if(ts)
			{
				block_line1 = tl1;
				if(block_stat != 1)
					block_col1 = tc1;
			}
			else
			{
				eof;
				block_begin;
				tof;
				block_end;
			}
		}
	}
	else
		Make_Message('Erased buffer ' + str(buf_num));

	file_changed = false;
exit:
	switch_window( source_win );
	if ( Parse_Int("/O=", MParm_Str) )
		Block_Off;
	pop_undo;
	refresh = temp_refresh;
}

macro PASTE TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	paste

Description: Copies block from buffer into current file.

Parameters:  /B=n 	buffer number
						 /O=n  If non-zero then the marked block will be turned
									 turned off after the operation


							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int  buf_num, temp_refresh, source_win, buf_win ;
	str  buf_name ;

	push_undo;
	temp_refresh = refresh;
	refresh = false;
	source_win = cur_window;
	error_level = 0;

	buf_num = parse_int('/B=', Mparm_Str);
	if ( buf_num > 999 )
		buf_num = 999;
	if ( buf_num < 0 )
		buf_num = 0;
	buf_name = _BUFFER_NAME;

	if(  switch_file(buf_name) == 0  )
	{
bufferempty:
		make_message('Nothing to paste in buffer ' +str(buf_num));
		switch_window( source_win );
		goto exit;
	}

	// window_attr = $81;
	if(  block_stat == 0  )
		goto bufferempty;
	buf_win = cur_window;
	switch_window( source_win );
	window_copy( buf_win );

	if( global_int("CURSOR_EOB"))
	{
		int tpersist = persistent_blocks;
		persistent_blocks = TRUE;
		if( block_stat != 2 )
			rm("ENDBLOCK");
		switch ( block_stat )
		{
			case 1 :
				down;
				break;
			case 2 :
				goto_col( block_col2 + 1 );
				break;
			case 3 :
				right;

		}
		persistent_blocks = tpersist;
	}
	make_message( 'Block copied from buffer ' + str(buf_num));
exit:
	if ( Parse_Int("/O=", MParm_Str) )
		Block_Off;
	pop_undo;
	refresh = temp_refresh;
}

macro BLCKMATH TRANS2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: BLCKMATH

Description:  Performs math operations on blocks of numbers.

Parameters: /O=  [ * / - +]   the math operation to perform.
												If /O= is not specified, the user will be
												prompted for the operation.
						/BC=	Amount of boxes to kill upon exit.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	real  rx, ry ;
	str  tstr[80], operation[1] ;
	int  jx, tbc , commas, jy,Negative ,
			t_persistent_blocks = persistent_blocks;

	refresh = false;
	persistent_blocks = true;

	operation = parse_str('/O=', mparm_str );
	Commas = false;
	if(  operation == ''  ) {
		tbc = parse_int('/BC=', mparm_str);
		RM('userin^xmenu /L=Select Operation/B=1' + mparm_str +
				'/M=+  (BLOCKMATH)-  ()*  ()/  ()');
		if(  return_int < 1  ) {
			goto exit;
		}
		operation = copy('+-*/', return_int, 1 );
		while(  box_count > tbc  ) {
			kill_box;
		}
	}

	Push_Undo;
	mark_pos;
	if(  marking  ) {
		block_end;
	}
	rx = 0.0;
	jx = 0;
	goto_line( block_line1 );
	if(  block_stat == 2  ) {
		while(  (c_line <= block_line2)  ) {
			goto_col( block_col1 );
	 col_again:
			while(  NOT(at_eol) &
						(XPOS(cur_char,'0123456789-.',1) == 0) &
						(c_col < block_col2)  ) {
				RIGHT;
			}
			if(  (c_col <= block_col2)  ) {
				tstr = get_word_in( '0123456789.-,' );
				call perform_operation;
				if(  (c_col < block_col2) & NOT( at_eol )  ) {
					goto col_again;
				}
			}
			down;
		}
	} else {
		goto_col( 1 );
		goto_col( block_col1 );
		if(  (block_stat == 1) | (block_stat == 3)  ) {
			while(  (c_line <= block_line2)  ) {
				while(  NOT(at_eol) &
							(XPOS(cur_char,'0123456789-.',1) == 0)
							 ) {
					RIGHT;
				}
				if(  (at_eol) | ( (c_line == block_line2) & (c_col > block_col2))  ) {
					down;
					first_word;
				} else {
					tstr = get_word_in( '0123456789.-,' );
					call perform_operation;
				}
			}
		}
	}
	goto_mark;
	tstr = rstr( rx, 0, 10 );
	if(  xpos( '.', tstr, 1 )  ) {
		while(  (copy( tstr, svl(tstr),1 ) == '0')  ) {
			tstr = copy( tstr, 1, svl(tstr) - 1 );
		}
		if(  (copy( tstr, svl(tstr),1 ) == '.')  ) {
			tstr = copy( tstr, 1, svl(tstr) - 1 );
		}
	}
	if(  (Commas)  ) {
		Negative = (Xpos('-',Tstr,1) > 0);
		Jx = Xpos('.',Tstr,1);
		if(  (Jx == 0)  ) {
			Jx = Svl(tstr);
		} else {
			--jx;
		}
		Jx = Jx - Negative;
		Jy = (jx / 3) - ((Jx % 3) == 0);
		while(  (Jy)  ) {
			tstr = Str_Ins(',',tstr,(jx - (Jy * 3)) + 1 + Negative);
			++jx;
			--Jy;
		}
	}
	text( tstr );
	Pop_Undo;
	goto exit;

perform_operation:
remove_commas:
	Jy = XPos(',',Tstr,1);
	if(  (Jy)  ) {
		Commas = True;
		Tstr = Str_Del(TStr,Jy,1);
		Goto REMOVE_COMMAS;
	}
	if(  rval( ry, tstr ) == 0  ) {
		++jx;
		if(  jx == 1  ) {
			rx = ry;
		} else {
			if(  operation == '+'  ) {
				rx = rx + ry;
			}
			if(  operation == '*'  ) {
				rx = rx * ry;
			}
			if(  operation == '-'  ) {
				rx = rx - ry;
			}
			if(  operation == '/'  ) {
				rx = rx / ry;
			}
		}
	}
	ret;
exit:
	persistent_blocks = t_persistent_blocks;
}
