//+++keyword+++ "@(#)%v %n, %f, %w"
// "@(#)47 VCS.S, 31-Jan-94,23:36:02, LDH"

macro_file VCS;

#include exit.sh
#include mesys.sh

#define vcs_version "7.00g"

/******************************************************************************
														 Multi-Edit Macro File
                               27-Jul-93  14:37

	Interfaces to the following Version Control Software packages are supported.

		TLIB by Burton Systems Software
    PVCS by Intersolv
		RCS by Mortice Kern Systems or GNU
    Sourcerers Apprentice by Borland (Solution Systems)
    SourceSafe by One Tree Software

	To configure Multi-Edit to interface to any of the supported Version
	Control System packages, run the macro VCS and select the "Configure..."
	Option from the menu and then select the "Install VCS support in STARTUP.S"
	button.  Then something like the following will be place in your STARTUP.S
	macro:

		//****** VCS Setup ******
      rm('VCS^VCS_RESTORE');
		//***********************


	VCS           - Main VCS menu.
	VCS_Cmds      - The main VCS independent command interface.
	VCS_Get_File  - Allows retrieval of latest revision of VCS file.
	VCS_Dir       - Displays and allows selection of VCS log files.
	VCS_Cleanup   - Makes sure user is aware of checked out files.
	VCS_Ext       - Converts file extension to VCS LIBEXT extension.
	VCS_Path      - Gets the VCS path from the global variable !VCSID.
	VCS_Xlate     - Translates Return_Str to expand <LPATH>, and so on.
	VCS_Status    - Saves current VCS status (files checked out, etc.)
	VCS_Restore   - Restores VCS status (files checked out, etc.)
	VCS_LFile     - Search for a library file in a given path.
	VCS_Configure	- Displays a dialog box to allow configuring the VCS interface.
	VCS_Save      - Saves and compiles STARTUP.S file for the selected VCS type.
  VCS_Keys      - Adds the VCS quick keys to the Keymap.
  VCS_Add_to_User - Adds the VCS Support menu to the User Menu.
	VCS_Init		  - Initializes the VCS System.
  VCS_Chg_Cmt   -
  VCS_SS_Cmds   - The SourceSafe command interface.
  VCS_SS_File   - Creates a file dialog box for the SourceSafe VCS package.
  VCS_SSLogin   - Check if a SourceSafe user is logged in, and prompts if not.
  VCS_File      - Creates a file dialog box for the the other VCS packages.
	VCS_XMacro    - Sample user written xlate macro.

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

macro VCS trans2 {
/******************************************************************************
															 Multi-Edit Macro
                               27-Jan-92  08:28

	Macro   : VCS  (Version Control System).
	Function: Main VCS Menu.
  Syntax  : rm('VCS');
	Entry   : /ED=1 to allow editing the menu.
	Exit    : Executes the selected option.

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

//+++keyword+++ "@(#)%v %n, %f, %w"
	str rev_id = "@(#)47 VCS.S, 31-Jan-94,23:36:02, LDH";

  rm("MENU /ED=1/MN=VCS.DB^VCS" + mparm_str);           // TMJ 01-27-92 08:28am

}  // VCS

macro vcs_cmds trans2 {
/******************************************************************************
															 Multi-Edit Macro
                               03-Jan-94  19:22

	Macro   : VCS_Cmds
	Function: The main VCS independent command interface
  Syntax  : rm('VCS_Cmds /C=str/F=str/P=str/CF=1/NO_EXT=1/ND=1');
	Entry   : /C=str    Command string
							 BROWSE Get file out of library for browsing.
							 GET    Get file out of library for modifications.
							 LIST   List file revisions.
							 LOCK   Lock a library file without checking it out.
							 PUT    Put file back in library.
							 UNLOCK Unlock and delete a checked out file.
							 VIEW   View the changes to the current file.
						/F=str    File name
						/P=str    Library path for output files
						/ND=1     NO deletion of the current window will take place
            /NO_EXT=1 NO rm('EXTSETUP') after loading file TMJ 10-02-91 12:12pm
						/DF=1			Called from Directory of files
	Exit    : Return_Int
							 0      Error
							 1      No Error

                Copyright (C) 1990-94 by Small Systems Support.
*********************************************************************(ldh)** */

  str pkg[8] = shorten_str(parse_str("ID=", global_str("!VCSID"))),
      message_line;

  if ((pkg == "") || (pkg == "NONE")) {
		return_int = 0;
		goto None_Exit;
  }

  if (pkg == "SS" && global_int("!VCSNOSS")) {
    message_line = "SourceSafe support not active, Exit, run SSINIT, then ME.";
    goto none_exit;
  }

  str tstr[128],
			vcs_cmd[64],
      VCS_name[128],
			chk_mode[10],
      fname[128] = parse_str('/F=', mparm_str),
      lpath[128] = parse_str('/P=', mparm_str),
			cmd[20] = caps(shorten_str(parse_str('/C=', mparm_str))),
			chg_flg_start[20] = parse_str('CFS=', global_str('!VCSID')),
			chg_flg_end[20] = parse_str('CFE=', global_str('!VCSID')),
			wrk_dir = caps(global_str('!VCSWRKDIR')),
			tfname[128],                       // Tmp file name
			vcs3_tmp = user_id + "VCS3.TMP",
			new_ext[3]
			;


	int	jx,
			LCol,
			LRow,
			LLine,
			old_win,
			VCS_win,
			file_win = 0,
			sav_show,
			check_num,
			initial_rev,
			multi_line_comment,
			org_win = window_id,
      checkout_count = global_int('!VCS_' + pkg + '_COUNT!'),
			show_cmdline = parse_int('/SCL=', global_str('!VCS')),
			sr = reg_exp_stat,       					// Save regular exprssion status
			si = ignore_case,        					// Save ignore case variable
			srf = refresh,           					// Save refresh variable
			snp = global_int('!VCSSNP'),      // Save No Prompt global variable
			cmd_in_win = parse_int('/DW=', global_str('!VCS')),
      cdif = FALSE,
      sav_ciw,                          // Save Command in Window variable
			chg_support = parse_int('CSP=', global_str('!VCSID')),
			chg_flag = FALSE,
			sro,                              // Save read_only status
      rvc = parse_int('RVC=', global_str('!VCSID')),
      bsav = parse_int('/BS=', global_str('!VCS')),
      use_cd = parse_int('/UCD=', global_str('!VCS')),
      use_cmd,
			fnc = FALSE,                      // File name changed flag (VIEW)
			df = parse_int("/DF=", mparm_str),
			libfnd = TRUE
			;

	reg_exp_stat = TRUE;
	ignore_case = TRUE;
	refresh = FALSE;

  if (pkg == 'RCS')
    use_cmd = TRUE;
  else
    use_cmd = FALSE;

  set_global_str('!VCSWRKDIR', '');
	make_message('');
	if (df && ((cmd == "BROWSE") || (cmd == "GET"))) {
		if ((file_name != "?No-File?")) {
			if (fname == "") {
				switch_window(window_count);
				create_window;
			}
		}
	}
	old_win = window_id;

	// Mod to handle correct placement of .TMP files
	rm("TMP_FILE_NAME /FN=VCS.TMP");
	VCS_name = return_str;
	if (switch_file(VCS_name))
		erase_window;
	else {
		switch_window(window_count);
		create_window;
	}
	window_attr = $81;
	VCS_win = window_id;
	switch_win_id(old_win);

	if (lpath == "") {
		if (fname == "") {
			fname = file_name;
		}
		else {
			fname = fexpand(fname);
		}
		return_str = get_extension(fname);
  	rm("vcs^vcs_ext");
		new_ext = return_str;
    rm("vcs_path /F=" + fname + "/#=1");
  	rm("vcs_lfile /F=" + truncate_path(truncate_extension(fname))
				+ "." + new_ext);
		libfnd = return_int;
		lpath = return_str;
	}
	else {
    if (fname == "") {
      if ((pkg == "RCS") || (pkg == "SS")) {
				if (use_cd) {
					fname = fexpand(truncate_path(lpath));
				}
				else if (wrk_dir != "") {
					fname = wrk_dir + truncate_path(lpath);
				}
				else {
					fname = fexpand(lpath);
				}
			}
      else {
				make_message("Checking for file...");
        if (pkg == "PVCS") {
					fname = fexpand(truncate_path(lpath));
					sav_show = show_cmdline;
					sav_ciw = cmd_in_win;
					show_cmdline = FALSE;
					cmd_in_win = 0;
					Return_Str = Parse_Str('LC=', Global_Str('!VCSID'));
					call VCS_Exec;
					load_file(VCS_name);
					if ((!error_level) && (search_fwd('Workfile:', 0))) {
						fname = get_word(' ');
						right;
						search_fwd('[~ ]', 1);
					}
					else {
						goto error_exit;
					}
					cmd_in_win = sav_ciw;
					show_cmdline = sav_show;
				}
				else {
					switch_win_id(VCS_win);
					load_file(lpath);
					if (error_level) {
						goto error_exit;
					}
          if ((pkg == 'TLIB4') || (pkg == 'TLIB5')) {
						fname = get_word(' ');
						right;
					}
          else if (pkg == 'SAP') {
						search_fwd('|12', 1);
						right;
					}
					else {
						goto error_exit;
					}
				}
        if (use_cd) {
          fname = fexpand(truncate_path(get_word(' ')));
				}
        else if (wrk_dir != '') {
          fname = wrk_dir + truncate_path(get_word(' '));
				}
        else {
          fname = fexpand(get_word(' '));
				}
				erase_window;
			}
		}
		else if (use_cd) {
			fname = fexpand(truncate_path(fname));
		}
    else if (wrk_dir != '') {
      fname = wrk_dir + truncate_path(fname);
		}
//     else
//       fname = fexpand(fname);
	}
	if (cmd == "VIEW") {
		if (!libfnd) {
    	message_line = "Path to look for library file.";
    	call get_vcs_lpath;
    	if (pkg == "SS") {
      	return_str = lpath;
      	rm("vcs_ss_cmds /C=DIR/F="+ fname);
    	}
    	else {
      	return_int = file_exists(get_path(lpath)
						+ truncate_path(truncate_extension(fname)) + "." + return_str);
    	}
			if (!return_int) {
        message_line = pkg + " library file not found.";
        return_int = FALSE;
        goto exit;
			}
		}
		working;
		if (switch_file(fname)) {
			if (file_changed)
        rm('MEUTIL1^SAVEFILE /NP=1');
		}

    if (rvc) {
			// Mod to handle correct placement of .TMP files
			rm("TMP_FILE_NAME /FN=" + truncate_path(file_name));
			tfname = get_path(return_str) + truncate_path(file_name);
			if (file_exists(tfname)) {
				del_file(vcs3_tmp);
				rename_file(tfname, vcs3_tmp);
				fnc = TRUE;
			}
    }
    message_line = 'Viewing changes made to "' + truncate_path(fname) + '"...';
		return_str = parse_str('DC=', Global_Str('!VCSID'));
    cdif = parse_int('/CD=', global_str('!VCS'));

		call VCS_Exec;
    if (return_int) {
      if (rvc) {
        switch_win_id(old_win);
				rm('cdiff /F=' + tfname + '/D=' + str(!cdif));
        message_line = "CDIFF complete.";

        if (return_int == 1) {
          message_line = "Files the same.";
        }

        set_file_attr(tfname, 0);
        del_file(tfname);
      }
      else {
        load_file(VCS_name);
        if (!error_level) {
          read_only = TRUE;
          rm('UserIn^EditWindow /W=77/L=' + str(screen_length - 10) +
                                '/H=VCS^VCS_LIST/T=(' + pkg + ') Differences');
          Read_Only = FALSE;
          message_line = pkg + " " + cmd + "complete.";
        }
        else {
          goto error_exit;
				}
      }
    }
		if (rvc && fnc) {
			rename_file(vcs3_tmp, tfname);
		}
	}
	else if (cmd == "LIST") {
		if (!libfnd) {
    	message_line = "Path to look for library file.";
    	call get_vcs_lpath;
    	if (pkg == "SS") {
      	return_str = lpath;
      	rm("vcs_ss_cmds /C=DIR/F="+ fname);
    	}
    	else {
      	return_int = file_exists(get_path(lpath)
						+ truncate_path(truncate_extension(fname)) + "." + return_str);
    	}
			if (!return_int) {
      	message_line = pkg + " library file not found.";
      	return_int = FALSE;
      	goto exit;
			}
		}
		message_line = 'Listing versions of "' + truncate_path(fname) + '"...';
		Return_Str = Parse_Str('LC=', Global_Str('!VCSID'));

		call VCS_Exec;
    if (return_int) {
      load_file(VCS_name);
      if (!error_level) {
        Read_Only = TRUE;
        rm('UserIn^EditWindow /W=77/L=' + str(screen_length - 10) +
                              '/H=VCS^VCS_LIST/T=(' + pkg + ') Versions');
        Read_Only = FALSE;
        message_line = pkg + " " + cmd + " complete.";
      }
      else {
        goto error_exit;
			}
    }
	}
	else if (cmd == "LOCK") {
		if (!libfnd) {
    	message_line = "Path to look for library file.";
    	call get_vcs_lpath;
    	if (pkg == "SS") {
      	return_str = lpath;
      	rm("vcs_ss_cmds /C=DIR/F="+ fname);
    	}
    	else {
      	return_int = file_exists(get_path(lpath)
						+ truncate_path(truncate_extension(fname)) + "." + return_str);
    	}
			if (!return_int) {
        message_line = pkg + " library file not found.";
        return_int = FALSE;
        goto exit;
			}
		}
		call chk_lock;
		if (!return_int) {
			goto Error_Exit;
		}
		if (file_exists(fname)) {
			del_file(vcs3_tmp);
			rename_file(fname, vcs3_tmp);
		}
		message_line = 'Locking "' + fname + '"...';
		return_str = Parse_Str('OC=', Global_Str('!VCSID'));
		call VCS_Exec;
    if (return_int) {
      if (exit_code) {
        switch_win_id(vcs_win);
        TOF;
        read_only = TRUE;
        rm('USERIN^EditWindow /W=77/L=' + str(screen_length - 10) +
                              '/H=VCS^VCS_ERROR/T=' + pkg +
            ' Error');
        read_only = FALSE;
        goto Error_Exit;
      }

      switch_win_id(old_win)
      if (file_name == '?No-File?') {
        refresh = TRUE;
        rm('WINDOW^DelWin');
        refresh = FALSE;
        old_win = window_id;
      }
      if (file_exists(fname)) {
       	del_file(fname);
      }
      call Check_VCS_Out;
      if (!check_num) {
        ++checkout_count;
        check_num = checkout_count;
      }
      chk_mode = 'L';
      Set_Global_Str('!VCS_' + pkg + '_OUT#' + str(check_num),
                        '/F=' + fname + '/P=' + lpath + '/CM=' + chk_mode);
      message_line = 'Locked ' + truncate_path(fname) + '" out of"' + lpath;
    }
		rename_file(vcs3_tmp, fname);
  }
	else if ((cmd == "GET") || (cmd == "BROWSE")) {
		if (!libfnd) {
    	message_line = "Path to look for library file.";
    	call get_vcs_lpath;
    	if (pkg == "SS") {
      	return_str = lpath;
      	rm("vcs_ss_cmds /C=DIR/F="+ fname);
    	}
    	else {
      	return_int = file_exists(get_path(lpath)
						+ truncate_path(truncate_extension(fname))
						+ "." + return_str);
    	}
			if (!return_int) {
        message_line = pkg + " library file not found.";
        return_int = FALSE;
        goto exit;
			}
		}
		return_int = TRUE;
		if (cmd == "GET") {
			call chk_lock;
			if (!return_int) {
				goto error_exit;
			}
			vcs_cmd = parse_str("GC=", global_str("!VCSID"));
		}
		else {
			vcs_cmd = parse_str("BC=", global_str("!VCSID"));
		}
		if (switch_file(fname)) {
      if ((!read_only) || (df)
					|| (truncate_path(file_name) != truncate_path(fname)))
			{
        rm("userIn^verify /H=VCS^VCS_VERIFY/T=" + fname
						+ " Already loaded, OVERWRITE? /BL= ARE YOU SURE? ");
			}
			if (return_int) {
				lrow = c_row;
				lline = c_line;
				lcol = c_col;
				file_win = window_id;
				sro = read_only;
				erase_window;
			}
		}
		else if (file_exists(fname)) {
      rm("userIn^verify /H=VCS^VCS_VERIFY/T=" + fname
					+ " Already exists, OVERWRITE? /BL= ARE YOU SURE? ");
		}
		if (!return_int) {
      message_line = pkg + " " + cmd + " command aborted.";
			goto exit;
		}
		if (file_exists(fname)) {
			set_file_attr(fname, 0);
			del_file(vcs3_tmp);
			rename_file(fname, vcs3_tmp);
		}
		message_line = "Retrieving \"" + fname + "\"...";
		return_str = vcs_cmd;
		call vcs_exec;
    if (return_int) {
      if (exit_code) {                  // error
        load_file(VCS_name);
        if (!error_level) {
          tof;
          read_only = TRUE;
          rm("userIn^editWindow /W=77/L=" + str(screen_length - 10)
							+ "/H=VCS^VCS_ERROR/T=" + pkg + " Error");
          read_only = FALSE;
        }
        if (rename_file(vcs3_tmp, fname ) == 0) {
					if (sro) {
						set_file_attr(fname, 0x01);
					}
        	if (switch_win_id(file_win)) {
            load_file(fname);
						if (!error_level) {
            	call reset_file;
        			rm("extSetup");
						}
          }
        }
        goto error_exit;
      }
      if ((pkg == "SAP") && (cmd == "GET")) {
        set_file_attr(fname, 0);
			}
      if (switch_win_id(file_win)) {
        load_file(fname);
				if (!error_level) {
          call reset_file;
        	if (old_win != file_win) {
          	switch_win_id(old_win);
          	delete_window;
          	switch_win_id(file_win);
          	old_win = file_win;
        	}
				}
      }
      else {
        switch_win_id(old_win);
        rm("userIn^checkFile");
        if (return_int < 1) {
          message_line = "\"" + truncate_path(fname) + "\" checked out of \""
							+ lpath + "\" but not loaded.";
          return_int = FALSE;
          goto exit;
        }
        load_file(fname);
      }
			if (!parse_int("/NO_EXT=", mparm_str)) {
        rm("extSetup");
			}
			call check_vcs_out;
      if (cmd == "GET") {
        message_line = "Modify \"";
        chk_mode = "M";
      }
      else {
        Read_Only = True;
        message_line = "View \"";
        chk_mode = "V";
      }
      if (!((chk_mode == "V") && (bsav == 0))) {
      	if (!check_num) {
        	check_num = ++checkout_count;
      	}
        set_global_str("!VCS_" + pkg + "_OUT#" + str(check_num),
                       "/F=" + file_name + "/P=" + lpath + "/CM=" + chk_mode);
			}
      message_line += truncate_path(fname) + "\" out of \"" + lpath + "\".";
    }
    else {
      if (switch_win_id(file_win)) {
      	rename_file(vcs3_tmp, fname);
				if (sro) {
					set_file_attr(fname, 0x01);
				}
        load_file(fname);
				if (!error_level) {
          call reset_file;
        	rm("extSetup");
				}
      }
      switch_win_id(old_win)
      if (df && (caps(file_name) == "?NO-FILE?")) {
        delete_window;
        old_win = org_win;
      }
    }
  }
	else if ((cmd == "PUT") || (cmd == "UNLOCK")) {
		chk_mode = "";
		call check_vcs_out;

		if ((chk_mode != "V") && (chk_mode != "L")) {
			call chk_lock;
			if (!return_int) {
				goto error_exit;
			}
		}
		if (chk_mode == "L") {
			cmd = "UNLOCK";
		}
		if (cmd == "UNLOCK") {
			message_line = "Unlocking \"" + truncate_path(fname) + "\"...";
			vcs_cmd = parse_str("UC=", global_str("!VCSID"));
			return_int = TRUE;
			error_level = 0;
      if (switch_file(fname)) {
				LRow = C_Row;
				LLine = C_Line;
				LCol = C_Col;
        file_win = window_id;
      }
		}
		else {
			if (!switch_file(fname)) {
				switch_window(window_count);
				create_window;
				error_level = 0;
				load_file(fname);
				if (error_level) {
					delete_window;
					goto error_exit;
				}
			}
			LRow = C_Row;
			LLine = C_Line;
			LCol = C_Col;
			file_win = window_id;
			if (chk_mode == "V") {
				erase_window;
				set_file_attr(fname, 0);
				if (file_exists(fname)) {
					del_file(fname);
				}
				goto put_done;
			}
			if (initial_rev = !libfnd) {
        message_line = "Path to save library file to.";
				lpath = get_path(lpath);
        call get_vcs_lpath;
      	if (pkg == "SS") {
					return_str = lpath;
        	rm("vcs_ss_cmds /C=DIR/F=" + fname);
					initial_rev = !return_int;
				}
				else {
      			initial_rev = (file_exists(get_path(lpath)
								+ truncate_path(truncate_extension(fname))
								+ '.' + return_str) == FALSE);
				}
			}
			if (initial_rev) {
				VCS_Cmd = Parse_Str('CC=', Global_Str('!VCSID'));
        message_line = '(' + pkg + ') Describe "' + truncate_path(fname) + '".';
        cmd = 'CREATE';
      }
			else {
				VCS_Cmd = Parse_Str('PC=', Global_Str('!VCSID'));
        message_line = '(' + pkg + ') Describe changes to "' +
											truncate_path(fname) + '".';
			}
      if (pkg == 'SAP')
				multi_line_comment = FALSE;
			else
				multi_line_comment = parse_int('/MLC=', global_str('!VCS'));

			// Change Comment Support

      if ((chg_flg_start != '') && (chg_flg_end != ''))
			{
				tof;
				goto_col(1);
				reg_exp_stat = FALSE;
				if (search_fwd(chg_flg_start, 1)) {
					block_begin;
					right;
					if (chg_flag = search_fwd(chg_flg_end, 50)) {
						block_end;
            if (!chg_support)
              rm('meutil2^cut /B=1');
            else
              block_off;
					}
				}
				set_global_int("!VCSCHG_FLG", chg_flag);
				reg_exp_stat = TRUE;
			}
			return_int = TRUE;
			if (!(chg_support && chg_flag)) {
				switch_window(window_count);
				create_window;
				// Mod to handle correct placement of .TMP files
				rm("TMP_FILE_NAME /FN=VCS2.TMP");
				file_name = return_str;

				if (chg_flag) {
          rm('meutil2^paste /B=1/O=1');
				}
				else if (file_exists(file_name)) {
					load_file(file_name);
				}
				if (multi_line_comment) {
          rm('USERIN^EDITWINDOW /A=1/W=77/L=10/WW=1/RM=72/CC=1/SP=1/T=' +
							message_line + '/B=1/H=VCS^VCS_MESSAGE');
				}
				else {
					create_global_str('!V_ISTR_1', get_line);
					return_str = file_name;
					erase_window;
					file_name = return_str;
					create_global_str('!V_IPARM_1', '/L=1/W=75/ML=128/H=VCS^VCS^VCS_CMNT');
          rm( 'USERIN^DATA_IN /PRE=!V_/#=1/T=' + Message_Line);
					if (return_int) {
						return_str = global_str('!V_ISTR_1');
      			set_global_str("@VCS_MESSAGE!", return_str);
						text(return_str);
					}
					set_global_str('!V_ISTR_1', '');
				}
				if (!return_int) {
					delete_window;
					if (switch_win_id(file_win)) {
						block_off;
						call reset_file;
					}
					message_line = pkg + " " + cmd + " command aborted.";
					goto exit;
				}
				save_file;
				delete_window;
			}
			message_line = 'Storing "' + truncate_path(fname) + '"...';
		}
		if (return_int) {
			if (switch_win_id(file_win)) {
				if (chg_flag && block_stat)
					delete_block;
				if (file_changed)
//          rm('MEUTIL1^SAVEFILE /NP=1');
					save_file;
				erase_window;
			}
			working;
			return_str = vcs_cmd;
			call vcs_exec;
      if (return_int) {
        if (exit_code) {
          load_file(VCS_name);
          if (!error_level) {
            eof;
            home;
            read_only = TRUE;
            rm('USERIN^EditWindow /W=77/L=' + str(screen_length - 10) +
                                  '/H=VCS^VCS_ERROR/T=' + pkg +
              ' Error');
            read_only = FALSE;
          }
          if (switch_win_id(file_win)) {
            load_file(fname);
            if (!error_level) {
              if (chg_flag) {
                tof;
                rm('meutil2^paste /B=1/O=1');
              }
              call reset_file;
        			rm("extSetup");
            }
          }
        	goto error_exit;
        }

put_done:
        int del_src = parse_int("DSRC=", global_str("!VCSID")),
						res_file = parse_int("/FRL=", global_str("!VCS"));

        if (cmd == "UNLOCK") {
          message_line = "\"" + truncate_path(fname) + "\" unlocked.";
          if ((del_src) && file_exists(fname)) {
            set_file_attr(fname, 0);
            del_file(fname);
          }
        }
        else if (chk_mode == "V") {
          message_line = "\"" + truncate_path(fname) + "\" deleted.";
        }
        else if (initial_rev) {
          message_line = "\"" + lpath + truncate_path(fname) + "\" created.";
          if (del_src && file_exists(fname)) {
            rm("userIn^verify /H=VCS^VCS_VERIFY/BL=" + pkg
								+ " Library created/T=Delete \"" + fname + "\"?");
            if (return_int) {
              set_file_attr(fname, 0);
              del_file(fname);
            }
          }
        }
        else {
          message_line = "\"" + truncate_path(fname) + "\" checked into \""
							+ lpath + "\".";
          if (del_src && file_exists(fname)) {
          	set_file_attr(fname, 0);
            del_file(fname);
          }
				}
        if (check_num != 0) {
          return_Int = check_num;
          rm("userIn^deleteItem /G=!VCS_" + pkg + "_OUT#/#="
							+ str(checkout_count));
          --checkout_count;
        }
        if (switch_win_id(file_win)) {
          if (file_exists(fname) && res_file) {
            load_file(fname);
            if (!error_level) {
              call reset_file;
              if (!parse_int("/NO_EXT=", mparm_str)) {
                rm("extSetup");
							}
            }
          }
					else if (!parse_int("/ND=", mparm_str)) {
            refresh = TRUE;
            rm("window^delWin");
            refresh = FALSE;
            if (old_win == file_win) {
              old_win = window_id;
						}
          }
        }
      }
      else {
        if (switch_win_id(file_win)) {
          load_file(fname);
          if (!error_level) {
            if (chg_flag) {
              tof;
              rm("meutil2^paste /B=1/O=1");
            }
            call Reset_File;
       			rm("extSetup");
          }
        }
        goto exit;
      }
		}
	}
	return_int = TRUE;
	goto exit;

check_vcs_out:
	check_num = 0;
	jx = 1;
	while ((jx <= checkout_count) &&
        (parse_str('/F=', global_str('!VCS_' + pkg + '_OUT#' +
																			str(jx))) != fname))
	{
		++jx;
	}
	if (jx <= checkout_count) {
		check_num = jx;
    lpath = parse_str('/P=', global_str('!VCS_' + pkg + '_OUT#' +
																					str(jx)));
    chk_mode = Parse_Str('/CM=', global_str('!VCS_' + pkg + '_OUT#' +
																						 str(jx)));
	}
	ret;

get_vcs_lpath:
	call check_vcs_out;

  if ((check_num == 0) && (!snp)) {
		return_str = lpath;
    create_global_str('!V_ISTR_1', lpath);
		create_global_str('!V_IPARM_1', '/L=1/W=75/ML=128/H=VCS^LIBPATH');
    rm( 'USERIN^DATA_IN /PRE=!V_/#=1/T=(' + pkg + ') ' + message_line);
    if (return_int == 0) {
			message_line = pkg + " " + cmd + " command aborted.";
			Goto Exit;
		}

		lpath = Global_Str('!V_ISTR_1');
		Set_Global_Str('!V_ISTR_1', '');
		if ((SVL(lpath) > 0) && (copy(lpath, SVL(lpath), 1) != '\'))
			lpath = lpath + '\';
	}

	return_str = get_extension(fname);
  rm('VCS^VCS_Ext');
	ret;

vcs_exec:
  switch_win_id(vcs_win);
  rm("vcs_xlate /F=" + fname + "/P=" + lpath);

	if (show_cmdline && !snp) {
		create_global_str("!V_ISTR_1", return_str);
		create_global_str("!V_IPARM_1", "/L=1/W=75/ML=128/H=VCS^VCS_CMDLIN");
    rm("userIn^data_in /PRE=!V_/#=1/T=(" + pkg + ") " + cmd + " Command Line");
    if (!return_int) {
			message_line = pkg + " " + cmd + " command cancelled.";
      ret;
		}

		return_str = global_str("!V_ISTR_1");
		set_global_str("!V_ISTR_1", "");
	}
	int	prj_chg = FALSE;
  str sav_rstr = return_str;

	if ((cmd == "CREATE") && (pkg == "SS")) {
		if (lpath != global_str("!VCSSSPRJ")) {
			make_message("Changing to SS project " + lpath + ".");
			return_str = lpath;
			rm("vcs_ss_cmds /C=CP");          // Change to new project to add file
			if (return_int) {
				make_message("SS CP error");
				return_int = FALSE;
				ret;
			}
			prj_chg = TRUE;
		}
	}

	str t_dir = dir_path,
      n_path = get_path(fname);

	change_dir(str_del(n_path, svl(n_path), 1));
	if (error_level != 0) {
		make_message("Error in change_dir, CURDIR=" + dir_path);
    return_int = FALSE;
		ret;
	}

	str jrnl_path = "LPATH";
  int sem = environment_mode;

  if (pkg == "TLIB5") {
    environment_mode = TRUE;
    return_str = "<LPATH>";
    rm("vcs_xlate /P=" + lpath);
    set_environment(jrnl_path, return_str);
  }
	working;
  make_message(message_line);
  return_str = sav_rstr;
  rm("meUtil1^exec /SCREEN=" + str(cmd_in_win) + "/RED=" + vcs_name
			+ "/REDERR=STDOUT/CMDLN=1/CMD=" + str(use_cmd));
	change_dir(t_dir);
	if (pkg == "TLIB5") {
  	set_environment(jrnl_path, "");
  	environment_mode = sem;
	}
	if (prj_chg) {
	 	int	sri = return_int;
		sav_rstr = return_str;
		return_str = global_str("!VCSSSPRJ");
		make_message("Changing to SS project " + return_str + ".");
		rm("vcs_ss_cmds /C=CP");          // Change back to old project
		return_int = sri;
		return_str = sav_rstr;
	}
  ret;

Chk_Lock:
	return_int = TRUE;
  if ((pkg == 'TLIB4') || (pkg == 'TLIB5')) {

		str lok,
				lokfile,
				user = Get_Environment('TLIBID');

		if (user == '')
      if ((user = get_environment('USERNAME')) == '')
        user = parse_str('USERID=', global_str('!VCSID'));
		return_str = get_extension(fname);
    rm('VCS_Ext /LOK');
		lok = return_str;
		lokfile = truncate_extension(lpath) + '.' + lok;

		if (file_exists(lokfile)) {
			switch_win_id(VCS_win);
			load_file(lokfile);
			if (!error_level) {
        if (caps(user) != (lok = caps(get_word(' ')))) {
          rm('meerror^messagebox /B=1/T=VCS Error/M=' + lpath +
																										' is locked by ' + lok);
					return_int = FALSE;
				}
			}
			else
				return_int = FALSE;
      switch_win_id(old_win);
    }
	}
	else if (pkg == 'SS') {

    str lok = caps(global_str('!VCSSS_FUSR')),
        user = caps(global_str('!VCSSSUSER'));

    set_global_str('!VCSSS_FUSR', '');
    if ((lok != '') && (lok != user)) {
        rm('meerror^messagebox /B=1/T=VCS Error/M=' + lpath + ' is locked by '
            + lok);
        return_int = FALSE;
    }
  }
	ret;

reset_file:
	while (lrow > 1) {
		down;
		--lrow;
	}
	goto_line(lline);
	goto_col(lcol);
	ret;

Error_Exit:
  message_line = "Error #" + str(error_level) + " occurred during " + pkg
			+ " operation.";
	error_level = 0;
	return_int = FALSE;

Exit:
	if (switch_win_id(VCS_win))
		delete_window;

	del_file(vcs3_tmp);
	switch_win_id(old_win);

  if (return_int)
    del_file(VCS_name);
	else if (file_name == '?No-File?') {
		if (!parse_int('/ND=', mparm_str)) {
			refresh = TRUE;
      rm('WINDOW^DelWin');
			switch_win_id(org_win);
      return_int = FALSE;
		}
	}

	set_global_int('!VCSCHG_FLG', 0);
	set_global_int('!VCS_' + pkg + '_COUNT!', checkout_count);

	reg_exp_stat = sr;
	ignore_case = si;
	refresh = srf;

None_Exit:
	make_message(message_line);

}  // VCS_Cmds

macro vcs_get_file trans2 {
/******************************************************************************
															 Multi-Edit Macro
                               10-Dec-93  19:27

	Macro   : VCS_Get_File
	Function: Called from the LDFILES macro if the file was not found.  This
            will search for a VCS log file, and ask the user if they wants to
						retrieve the latest revision.  If file name was not passed then
						the current file is used if it is loaded readonly, this allows
						extracting a file for modification after the file was loaded for
						viewing only.
  Syntax  : rm('VCS_Get_File /F=str/C=cmd');
	Entry   : /F=   File name to extract.
						/C=   The desired command to execute (ie GET or BROWSE).
	Exit    : Return_Int
							 0      No File to extract.
							 1      Successful extraction.

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

  str pkg[8] = shorten_str(parse_str("ID=", global_str("!VCSID")));

  if ((pkg == "") || (pkg == "NONE")
				|| (pkg == "SS" && global_int("!VCSNOSS"))) {
    goto Error_Exit;
	}

  str new_ext[3],
			old_ext[3],
      ofile[128] = parse_str('/F=', mparm_str),
      gfile[128],
      tstr[128],
			cmd[8] = shorten_str(Parse_Str('/C=', MParm_Str)),
			heading,
			title
			;

	int jx,
			ldflag,
			sp,
			no_ext,											//TMJ 10-08-91 09:30am
			cfo = FALSE,
			snp = global_int('!VCSSNP')
			;


  ldflag = (cmd == '');
	no_ext = ldflag;								//TMJ 10-04-91 01:21pm
	if (ofile == '') {
		heading = ' already loaded';
		ofile = file_name;
		cfo = TRUE;                   // Extract current file
	}
	else
		heading = ' not found';

	gfile = ofile;
	tstr = truncate_path(ofile);
	old_ext = get_extension(tstr);

	Return_Str = old_ext;
  rm('VCS^VCS_Ext');
	new_ext = Return_Str;

  rm('VCS_Path /F=' + ofile);
  rm('VCS_LFile /F=' + truncate_extension(tstr) + '.' + new_ext);
	Error_Level = 0;
	if (Return_Int) {
		ofile = Return_Str;
		if (cmd == '') {
			sp = ((cmd = caps(global_str('!VCSGFM'))) == 'PROMPT');
			if (sp) {
				title = 'Extract from "' + ofile + '"?';
				jx = svl(title);

				Set_Global_Str('VCSIPARM_1', '/TP=10/L=1/C=1/T=' + title);
				Set_Global_Str('VCSIPARM_2','/TP=11/L=3/R=1/QK=5/T= to Modify /W=11');
				Set_Global_Str('VCSIPARM_3','/TP=11/L=3/R=3/QK=5/T= to View /W=9');
				Set_Global_Str('VCSIPARM_4','/TP=11/L=3/R=4/QK=5/T= to Lock /W=9');
				Set_Global_Str('VCSIPARM_5','/TP=11/L=3/R=0/QK=2/T= No /K1=27/K2=1/W=4');

				if (jx < 43)
					jx = 43;
				else
					jx = jx + 4;

        rm('CHECKEVENTS /M=4/G=VCSIPARM_/#=5/S=1/UC=1/W=' + Str(jx + 1));
        rm('USERIN^DATA_IN /PRE=VCS/#=5/S=2/T=(' + pkg + ') "' + tstr +
						heading + '"/H=VCS^VCS_GET_PROMPT');

				if (Return_Int == 1)
					cmd = 'GET';
				else if (Return_Int == 3)
					cmd = 'BROWSE';
				else if (Return_Int == 4)
					cmd = 'LOCK';
				else {
					Goto Error_Exit;
				}
			}
			else
				set_global_int('!VCSSNP', !sp);
		}

    rm('VCS_Cmds /NO_EXT=' + str(no_ext) + '/C=' + cmd + '/P=' + ofile + '/F=' +
								 gfile + '/CF=' + str(cfo));
		set_global_int('!VCSSNP', snp);
		return_int = TRUE;
	}
	else {

    if (cmd != '')
      make_message(pkg + ' library file not found.');

Error_Exit:
    return_int = FALSE;
	}
}  // VCS_Get_File

macro vcs_dir trans2 {
/******************************************************************************
															 Multi-Edit Macro
                               10-Dec-93  19:29

	Macro   : VCS_Dir
	Function: Displays directory listing of VCS log files.  Allows user to
						select a file to be retrieved for modification, viewing only,
						or listing the version history.
  Syntax  : rm('VCS_Dir /C=cmd');
	Entry   : /BC=	Amount of boxes to kill on exit.
						/C=		Command to execute (ie GET, BROWSE or LIST).
            /RC   Repeat command until <ESC>.
	Exit    : Runs VCS_Cmds with the selected file.

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

  int bc = parse_int('/BC=', mparm_str),
      rc = xpos('/RC', mparm_str, 1);

	str cmd[128] = caps(parse_str('/C=', mparm_str)),
      t_ext[3] = parse_str('LIBEXT=', global_str('!VCSID')),
  		pkg[8] = shorten_str(parse_str("ID=", global_str("!VCSID")));

  if ((pkg == "") || (pkg == "NONE")) {
    goto exit;
	}
  if (pkg == "SS" && global_int("!VCSNOSS")) {
    make_message("SourceSafe support not active, Exit, run SSINIT, then ME");
    goto exit;
  }

  if ((t_ext == '') || (t_ext == "%%%"))
		t_ext = '*';

  if (cmd == '')
    cmd = 'LIST';

  if (pkg == 'SS')
    rm('VCS_SS_File /T=(SS) Library Files' + '/RC=' + str(rc) + '/C=' + cmd);
  else {

Dir_Loop:
		rm('VCS_Path /F=' + file_name);

    return_str = return_str + '*.' + t_ext;
    rm('VCS_File /T=(' + pkg + ') Library Files/C=' + cmd);
    if (return_int > 0) {
      while (box_count > bc)
        kill_box;

      switch (return_int) {
        case 11:
          cmd = "GET";
          break;
        case 12:
          cmd = "LIST";
          break;
        case 13:
          cmd = "BROWSE";
          break;
        case 14:
          cmd = "LOCK";
          break;
      }
			if ((pos('*', return_str) > 0) || (pos('?', return_str) > 0)) {
				str tstr2 = get_path(return_str);
				if (first_file(return_str) == 0) {
					do {
						return_str = tstr2 + last_file_name;
      			rm('VCS_Cmds /C=' + cmd + '/P=' + return_str);
					} while (next_file == 0);
				}
			}
			else {
      	rm('VCS_Cmds /DF=1/C=' + cmd + '/P=' + return_str);
			}
      if (rc) {
        goto dir_loop;
			}
    }
  }
  rm('SetWindowNames');
  rm('FindWin');

exit:
}  // VCS_Dir

macro vcs_cleanup trans {
/******************************************************************************
															 Multi-Edit Macro
															 10-Dec-93  19:29

	Macro   : VCS_Cleanup
	Function: Checks to see if there are any checked out files and presents
						a menu giving the user the option to check them back in.
  Syntax  : rm('VCS_Cleanup');
	Entry   : /NX=1   NO Exit message flag.
						/CF=1   Current file ONLY.
						/ND=1   NO deletion of window being checked in.
	Exit    : Selected files written to Libraries.

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

	int out_cnt,
			jx,
			jy,
			old_win,
			srf = refresh,
			cfo = parse_int("/CF=", mparm_str),
			ndw = parse_int("/ND=", mparm_str), // TMJ 05-28-91 12:20pm
			bc = parse_int("/BC=", mparm_str),
			nx = parse_int("/NX=", mparm_str),
			snp = global_int("!VCSSNP"),
			evcnt,
			ro_file,
			menu = menu_create;

  str tmp_line,
			tmp_str,
			pkg[8] = shorten_str(parse_str("ID=", global_str("!VCSID"))),
			chk_mode;

	undo_stat = FALSE;
	messages = FALSE;
	refresh = FALSE;

  if ((pkg == "") || (pkg == "NONE")) {
		goto exit;
	}
	make_message("");
	while (box_count > bc) {
		kill_box;
	}
	if (!parse_int("/XIT=", global_str("!VCS")) && !nx) {
		goto save_exit;
	}

do_menu:
  out_cnt = global_int("!VCS_" + pkg + "_COUNT!");
	if (out_cnt < 1) {
		if ((nx == 1) && !cfo) {
      make_message("No " + pkg + " files checked out.");
		}
		return_int = TRUE;
		if (!nx) {
			goto save_exit;
		}
		else {
			goto exit;
		}
	}
	if (nx == 1) {
		nx = 2;                             //  Clear so no file message wont show
	}
	old_win = window_id;
	return_str = file_name;
	switch_window(window_count);
	create_window;
	jx = 0;
	while (++jx <= out_cnt) {
		tmp_line = "                                                                     ";
    tmp_str = parse_str("/F=", global_str("!VCS_" + pkg + "_OUT#" + str(jx)));
    chk_mode = parse_str("/CM=", global_str("!VCS_" + pkg + "_OUT#" + str(jx)));
		if ((tmp_str == return_str) || !cfo) { 	// TMJ 05-28-91 12:38pm
			jy = jx;                            	// TMJ 05-28-91 05:21pm
			ro_file = (chk_mode == "V");
			tmp_line = str_ins(truncate_path(tmp_str), tmp_line, 1);
			tmp_line = str_ins(chk_mode, tmp_line, 14);
			tmp_line = str_ins(get_path(tmp_str), tmp_line, 16);
      tmp_str = parse_str("/P=", global_str("!VCS_" + pkg + "_OUT#" + str(jx)));
			tmp_line = str_ins(tmp_str, tmp_line, 46);
			put_line(tmp_line);
			down;
		}
	}
	tof;
	if (at_eof) {                           	// TMJ 05-28-91 05:21pm
		delete_window;
		switch_win_id(old_win);
		return_int = TRUE;
		goto exit;
	}                                     		// TMJ 05-28-91 05:21pm
	if (cfo) {                             		// TMJ 05-28-91 04:14pm
		return_int = 1;
    if (!snp) {
			if (!ro_file) {
				menu_set_item(menu, 1,
						return_str + "\" checked out.",
						"",
						"/L=1/C=1",
						10, 0, 0);

				menu_set_item(menu, 2,
						"Check in",
						"",
						"/QK=1/L=3/C=1/R=1",
						11, 0, 0);

				menu_set_item(menu, 3,
						"Do NOT check in",
						"",
						"/QK=1/L=3/C=11/R=10",
						11, 0, 0);

				menu_set_item(menu, 4,
						"Unlock/Abandon",
						"",
						"/QK=1/L=3/C=28/R=6",
						11, 0, 0);

				evcnt = 4;
			}
			else {
				menu_set_item(menu, 1,
						return_str + "\" checked out for viewing.",
						"",
						"/L=1/C=1",
						10, 0, 0);

				menu_set_item(menu, 2,
						"Delete file",
						"",
						"/QK=1/L=3/C=1/R=1",
						11, 0, 0);

				menu_set_item(menu, 3,
						"Do NOT delete file",
						"",
						"/QK=4/L=3/C=14/R=10",
						11, 0, 0);

				menu_set_item(menu, 4,
						"Cancel",
						"",
						"/KC=<ESC>/K1=27/K2=1/L=3/C=34/R=0",
						11, 0, 0);

				menu_set_item(menu, 5,
						"Help",
						"",
						"/KC=<F1>/K1=0/K2=59/L=3/C=47/R=2",
						11, 0, 0);

				evcnt = 5;
			}
			return_int = menu;
      rm("userIn^data_in /HN=1/T=(" + pkg + ") File checked out/#=" + str(evcnt)
					+ "/S=2/H=VCS^VCS_DELWIN");
		}
	}
	else {
		menu_set_item(menu, 1,
				"Check in",
				"",
				"/KC=<ENTER>/K1=13/K2=28/R=1/L=1/C=13",
				11, 0, 0);

		menu_set_item(menu, 2,
				"Delete",
				"",
				"/KC=<DEL>/K1=0/K2=83/R=3/L=1/C=32",
				11, 0, 0);

		menu_set_item(menu, 3,
				"Unlock/Abandon",
				"",
				"/QK=1/R=6/L=1/C=47",
				11, 0, 0);

		menu_set_item(menu, 4,
				"Check In All",
				"",
				"/KC=<F3>/K1=0/K2=61/L=3/C=13/R=5",
				11, 0, 0);

		menu_set_item(menu, 5,
				"Delete All",
				"",
				"/KC=<F2>/K1=0/K2=60/L=3/C=31/R=4",
				11, 0, 0);

		menu_set_item(menu, 6,
				"Unlock All",
				"",
				"/KC=<F4>/K1=0/K2=62/L=3/C=47/R=7",
				11, 0, 0);

		evcnt = 6;
		if (!nx) {
			++evcnt;
			menu_set_item(menu, evcnt,
					"Exit no check in",
					"",
					"/KC=<ALTX>/K1=0/K2=45/L=5/C=27/R=8",
					11, 0, 0);
		}
		++evcnt;
		menu_set_item(menu, evcnt,
				"Select files:",
				"",
				"/DC=1/QK=1/C=1/W=75/L=" + str(5 + ((nx == 0) * 2))
						+ "/WIN=" + str(cur_window),
				15, 0, 0);

		return_int = menu;
    rm("userIn^data_in /HN=1/T=(" + pkg + ") Files checked out/#="
				+	str(evcnt) + "/S=" + str(evcnt) + "/H=VCS^VCS_CLEAN_MENU/Y="
				+ str(min_window_row));
	}
	if (cfo) {                            // TMJ 05-28-91 05:21pm
		jx = jy;                            // TMJ 05-28-91 05:21pm
	}
	else {                                // TMJ 05-28-91 05:21pm
		jx = c_line;                        // TMJ 05-28-91 05:21pm
	}
	delete_window;
	switch_win_id(old_win);
	if (!return_int) {
		goto exit;
	}
	else if (return_int == 10) {          // TMJ 05-28-91 05:21pm
		return_int = TRUE;                  // TMJ 05-28-91 05:21pm
		goto exit;                          // TMJ 05-28-91 05:21pm
	}
	if (return_int == 1) {                // Check in selected file.
    rm("vcs_cmds /C=PUT/ND=" + str (ndw)
				+ global_str('!VCS_' + pkg + '_OUT#' + str(jx)));
		if (!cfo) {                         // TMJ 05-28-91 12:38pm
			goto do_menu;
		}
	}
	else if (return_int == 3) {           // Delete selected file entry
    return_int = jx;
    rm("userIn^deleteItem /G=!VCS_" + pkg + "_OUT#/#=" + str(out_cnt));
		--out_cnt;
    set_global_int("!VCS_" + pkg + "_COUNT!", out_cnt);
		if (!cfo) {                         // TMJ 05-28-91 12:38pm
			goto do_menu;
		}
	}
	else if (return_int == 4) {						// Delete all checked out file entries
		jx = 0;
		while (++jx <= out_cnt) {
			return_int = jx;
      rm("userIn^deleteItem /G=!VCS_" + pkg + "_OUT#/#=" + str(jx));
		}
    set_global_int("!VCS_" + pkg + "_COUNT!", 0);
		goto do_menu;
	}
	else if (return_int == 5) {           // Check in all checked out files
		jx = 1;
		while (jx <= out_cnt) {
      rm("vcs_cmds /C=PUT/ND=" + str(ndw)
					+ global_str("!VCS_" + pkg + "_OUT#" + str(jx)));
			if (!return_int) {
				if (jx != out_cnt) {
					rm("verify /T=Continue Check In All?");
					if (!return_int) {
						break;
					}
				}
				++jx;
			}
			else {
  			out_cnt = global_int("!VCS_" + pkg + "_COUNT!");
			}
		}
		goto do_menu;
	}
	else if (return_int == 6) {           // Unlock selected file.
    rm("vcs_cmds /C=UNLOCK/ND=" + str(ndw)
				+ global_str("!VCS_" + pkg + "_OUT#" + str(jx)));
		if (!cfo) {
			goto do_menu;
		}
	}
	else if (return_int == 7) {           // Unlock all checked out files
		jx = 1;
		while (jx <= out_cnt) {
      rm("vcs_cmds /C=UNLOCK/ND=" + str(ndw)
					+ global_str("!VCS_" + pkg + "_OUT#" + str(jx)));
			if (!return_int) {
				if (jx != out_cnt) {
					rm("verify /T=Continue Unlock All?");
					if (!return_int) {
						break;
					}
				}
				++jx;
			}
			else {
  			out_cnt = global_int("!VCS_" + pkg + "_COUNT!");
			}
		}
		goto do_menu;
	}

save_exit:
	if (!cfo) {                           // TMJ 05-28-91 12:38pm
    rm("vcs^vcs_status");
	}

exit:
	menu_delete(menu);
	refresh = srf;
	messages = TRUE;
	undo_stat = TRUE;
}  // VCS_Cleanup

macro VCS_Ext trans2 {
/******************************************************************************
															 Multi-Edit Macro
															 05-Oct-91  22:55

	Macro   : VCS_Ext
	Function: Takes a file extension and converts it to the equivalent VCS
						LIBEXT or LOKEXT extension.
  Syntax  : rm('VCS_Ext /LOK');
	Entry   : Return_Str = The extent of the file to be loaded
						/LOK  to use LOKEXT
	Exit    : Return_Str = VCS file extent based upon the VCS LIBEXT or LOKEXT

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

	str new_ext[3];

	int jx;

	if (xpos('/LOK', mparm_str, 1))
		new_ext = parse_str('LOKEXT=', global_str('!VCSID'));
	else
		new_ext = parse_str('LIBEXT=', global_str('!VCSID'));

	if (new_ext == '')
		goto Exit;
	if (xpos('%', new_ext, 1)) {
		if (Copy(return_str, 3, 1) == '') {
			return_str = str_ins('%', str_del(return_str, 3, 1), 1);
			if (copy(return_str, 3, 1) == '') {
				return_str = str_ins('%', str_del(return_str, 3, 1), 1);
				if (copy(return_str, 3, 1) == '')
					return_str = str_ins('%', str_del(return_str, 3, 1), 1);
			}
		}
		else
			return_str = str_ins('&', str_del(return_str, 1, 1), 1);
	}
	else {
		jx = 1;
		while (jx = xpos('?', new_ext, jx)) {
			if ((copy(return_str, jx, 1) == '') && (jx != 3))
				new_ext = Str_Ins('_', str_del(new_ext, jx, 1), jx);
			else
				new_ext = str_ins(copy(return_str, jx, 1), str_del(new_ext, jx, 1), jx);
			++jx;
		}
		return_str = new_ext;
	}

Exit:
}  // VCS_Ext

macro VCS_Path trans2 {
/******************************************************************************
															 Multi-Edit Macro
															 25-Jun-90  21:31

	Macro   : VCS_Path
	Function: Gets the VCS path from the global variable !VCSID
  Syntax  : rm('VCS_Path /F=str/#=int');
	Entry   : /F=str       The file name to use.
						/#=int       The path entry # to return, 0 for all.
	Exit    : Return_Str = VCS Path based upon the VCS PATH.

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

	int counter, cnt, jx, jy;
  str t_path[128], fpath[128];

	cnt = Parse_Int('/#=', MParm_Str);
	t_path = parse_str('PATH=', global_str('!VCSID'));
	Return_Str = t_path;

	jy = 0;
	counter = 0;
	while (counter < cnt) {
		if (jy > Length(Return_str))
			Goto Exit;
		jx = XPos(';', Return_Str, jy + 1);
		if (jx == 0)
			jx = Length(Return_Str) + 1;
		t_path = Copy(Return_Str, jy + 1, jx - jy - 1);
		jy = jx;
		++counter;
	}

Exit:
	Return_Str = t_path;
  rm('VCS_Xlate ' + MParm_Str);
	if ((Length(return_str) > 0) &&
			(copy(return_str, length(return_str), 1) != '\'))
		return_str = return_str + '\';
}  // VCS_Path

macro VCS_Xlate trans2 {
/******************************************************************************
															 Multi-Edit Macro
                               24-Apr-93  13:41

	Macro   : VCS_Xlate
	Function: Translates Return_Str so that any occurrences of <LPATH>, <FILE>,
						<NAME>, <EXT>, <PATH>, <ME_PATH>, <USER_PATH>, <RCSDIR>, <OPATH>,
						<COMNT>, <SAPCMT> are expanded out.
  Syntax  : rm('VCS_Xlate /F=str/P=str');
  Entry   : /F=str  The file name to use.
            /P=str  The path to use for <LPATH>.
	Exit    : Return_Str  The expanded command line.
	Calls   : XlateCmdLine

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

	str fname[128], lpath[128], t_path, x_mac;
	int jx;

	fname = Parse_Str('/F=', MParm_Str);
	lpath = Parse_Str('/P=', MParm_Str);
	if (lpath == "") {
		lpath = Get_Path(fname);
	}
  while (jx = xpos('<LPATHX>', caps(return_str), 1)) {
    return_str = str_del(return_str, jx, 8);
    t_path = get_path(lpath);
    if ((svl(t_path) > 0) && (copy(t_path, svl(t_path), 1) == '\'))
      t_path = str_del(t_path, svl(t_path), 1);
    return_str = str_ins(t_path, return_str, jx);
	}

  while (jx = xpos('<LPATH>', caps(return_str), 1)) {
    return_str = str_del(return_str, jx, 7);
    t_path = get_path(lpath);
    return_str = str_ins(t_path, return_str, jx);
	}

  while (jx = xpos('<OPATH>', caps(return_str), 1)) {
    return_str = str_del(return_str, jx, 7);
    t_path = get_path(fname);
    if ((Svl(t_path) > 2) && (copy(t_path, 2, 1) == ':'))
      t_path = str_del(t_path, 1, 2);
    return_str = str_ins(t_path, return_str, jx);
	}

  while (jx = xpos('<COMNT>', caps(return_str), 1)) {
    t_path = str_del(return_str, jx, 7);
		// Mod to handle correct placement of .TMP files
		rm("TMP_FILE_NAME /FN=VCS2.TMP");
    return_str = str_ins(return_str, t_path, jx);
	}

  while (jx = xpos('<RCSDIR>', caps(return_str), 1)) {
    return_str = str_del(return_str, jx, 8);
    t_path = get_environment('RCSDIR');
    return_str = str_ins(t_path, return_str, jx);
	}

  while (jx = xpos('<SAPCMT>', caps(return_str), 1)) {
    return_str = str_del(return_str, jx, 8);
		if (!(global_int('!VCSCHG_FLG')))
      if (global_str('@VCS_MESSAGE!') != '')
        return_str = str_ins('-c"'+ global_str('@VCS_MESSAGE!') + '"',
														return_str, jx);
	}

  while (jx = xpos('<%TMP>', caps(return_str), 1)) {
    return_str = str_del(return_str, jx, 6);
    t_path = get_environment('TMP');
    if ((svl(t_path) > 0) && (copy(t_path, svl(t_path), 1) != '\'))
      t_path = t_path + '\';
    return_str = str_ins(t_path, return_str, jx);
  }

  while (jx = xpos('<TMP_PATH>', caps(return_str), 1)) {
    t_path = str_del(return_str, jx, 10);
    return_str = truncate_path(fname);
		// Mod to handle correct placement of .TMP files
		rm("TMP_FILE_NAME /FN=" + truncate_path(fname));
    return_str = str_ins(get_path(return_str), t_path, jx);
	}

	x_mac = parse_str('/UXM=', global_str('!VCS'));
	while (x_mac != '') {
		jx = xpos(';', x_mac, 1);
		if (jx == 0) {
			jx = svl(x_mac) + 1;
		}
		if (jx > 1) {
			rm(copy(x_mac, 1, jx - 1) + " " + mparm_str);
		}
		x_mac = str_del(x_mac, 1, jx);
	}

  rm('XlateCmdLine /F=' + fname);
}  // VCS_Xlate

macro VCS_Status trans {
/******************************************************************************
															 Multi-Edit Macro
                               04-Mar-93  00:15

	Macro   : VCS_Status
	Function: If the "Restore Status" box is checked in the configuration screen
						then the VCS system status is save in the file 'VCS_STAT.ME'.
  Syntax  : rm('VCS_Status');
	Entry   : None.
	Exit    : VCS status saved in 'VCS_STAT.ME' if restore status is on.
	Credit  : To Michael Crowe for defining and writing a similar function.

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

//+++keyword+++ "%n #%v, %d"
	str rev_id = "VCS.S #47, 31-Jan-94";

	int jx, Old_Window, temp_backup, checkout_count, VCS_Restore;
  str TStr;

	temp_backup = backups;
	backups = FALSE;
	Undo_Stat = FALSE;
	Refresh = FALSE;
	Messages = FALSE;

	VCS_Restore = parse_int('/VRS=', global_str('!VCS'));
	if (!VCS_Restore)
		Goto Exit;

	tstr = make_restr_name(VCS_Restore) + '.VCS';

  del_file(tstr);
  old_window = cur_window;
	switch_window(window_count);
	create_window;
	file_name = TStr;

	put_line(rev_id);
	down;

	tstr = first_global(jx);

	while (tstr != '') {
		if (xpos('!VCS_', tstr, 1)) {
			if (!xpos("!VCS_KEY", tstr, 1)) {
				if (jx == 1) {
					put_line('/G_INT=' + tstr);
					down;
					put_line(str(global_int(tstr)));
					down;
				}
				else {
					put_line('/G_STR=' + tstr);
					down;
					put_line(convert_string(global_str(tstr)));
					down;
				}
			}
		}
		tstr = next_global(jx);
	}

	if (c_line > 2)
		save_file;
	delete_window;

	switch_window(old_window);

Exit:
	refresh = TRUE;
	messages = TRUE;
	undo_stat = TRUE;
	backups = temp_backup;
}  // VCS_Status

macro VCS_Restore dump {
/******************************************************************************
															 Multi-Edit Macro
                               04-Mar-93  00:15

	Macro   : VCS_Restore
	Function: Restores status of the VCS system from the status file
						'VCS_STAT.ME'.
  Syntax  : rm('VCS_Restore');
	Entry   : None.
	Exit    : The VCS system is restored to last defined state.
	Credit  : To Michael Crowe for defining and writing a similar function.

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

	int i, T_Refresh, Old_Window, VCS_Restore;
  str tstr;

	Undo_Stat = FALSE;
	Messages = FALSE;
	T_Refresh = Refresh;
	Refresh = FALSE;

  rm('VCS_Init');

	Old_Window = Cur_Window;
	Switch_Window(Window_Count);
	Create_Window;

	VCS_Restore = parse_int('/VRS=', global_str('!VCS'));
	if (VCS_Restore == 0)
		Goto Exit;

	Error_Level = 0;
	Working;
	tstr = make_restr_name(VCS_Restore) + '.VCS';
	if (!file_exists(tstr))
		goto Exit;
	Load_File(tstr);

	if (Error_Level == 0) {
    rm('meerror^messagebox /NW=1/M=Restoring VCS Status...');
		down;
		while (!At_Eof) {
			TStr = Copy(Get_Line, 1, 7);
			if (TStr == '/G_INT=') {
				TStr = Copy(Get_Line, 8, 20);
				down;
				if (Val(i, Get_Line) == 0)
					Set_Global_Int(TStr, i);
			}
			else if (TStr == '/G_STR=') {
				TStr = Copy(Get_Line, 8, 20);
				down;
				Set_Global_Str(TStr, Reconvert_String(Get_Line));
			}
			down;
		}
		kill_box;
		Make_Message('VCS status restored.');
	}

Exit:
	Delete_Window;
	Switch_Window(Old_Window);

	Refresh = T_Refresh;
	Messages = TRUE;
	Undo_Stat = TRUE;
}  // VCS_Restore

macro VCS_LFile trans2 {
/******************************************************************************
															 Multi-Edit Macro
                               05-Oct-92  23:46

	Macro   : VCS_LFile
	Function: Search for a library file in a given path.
  Syntax  : rm('VCS_LFile /F=str');
	Entry   : /F=str      The filename
						Return_Str  Contains the path. (ie ";C:\TLIB\ME\;C:\TLIB\VCS").
	Exit    : Return_Int
							FALSE - File not Found.
							TRUE  - File Found.
						Return_Str  The found file name.

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

  str pkg[8] = shorten_str(parse_str("ID=", global_str("!VCSID")));

  str fname[128] = parse_str("/F=", mparm_str),
			ss_name,
      fpath[128];

  int ow = window_id,
      srf = refresh;

  if (pkg == "SS") {
		// Mod to handle correct placement of .TMP files
		str t_return_str = return_str;
		rm("TMP_FILE_NAME /FN=VCS_SS.TMP");
		ss_name = return_str;
		return_str = t_return_str;
    refresh = FALSE;
    rm("vcs_ss_cmds /C=DIR/F=" + fname);
    if (return_int) {
      switch_window(window_count);
      create_window;
      load_file(ss_name);
      if (!error_level) {
        forward_till(" \t\xFF");
        forward_till_not(" \t\xFF");
        set_global_str("!VCSSS_FUSR", get_word(" "));
        delete_window;
        switch_win_id(ow);
      }
    }
    else {
      set_global_str("!VCSSS_FUSR", "");
		}
    del_file(ss_name);
    refresh = srf;
  }
  else {

    int jx, jy;

    fpath = "";

    jy = 0;

    while (TRUE) {
      if (jy > length(return_str)) {
        return_int = FALSE;
        goto exit;
      }
      jx = xpos(";", return_str, jy + 1);
      if (jx == 0) {
        jx = length(return_str) + 1;
			}
      fpath = copy(return_str, jy + 1, jx - jy - 1);
      if ((svl(fpath) > 0) && (copy(fpath, SVL(fpath), 1) != "\\")) {
        fpath = fpath + "\\";
			}
      jy = jx;
      if (file_exists(fpath + fname)) {
        break;
			}
    }
    return_int = TRUE;
    return_str = fexpand(fpath + fname);
  }

exit:
}  // VCS_LFile

macro vcs_configure trans2 no_break {
/******************************************************************************
															 Multi-Edit Macro
															 10-Dec-93  19:26

	Macro   : VCS_Configure
	Function: Displays a dialog box to allow configuring the VCS interface.
  Syntax  : rm('VCS_Configure');
	Entry   : None.
	Exit    : VCS System Configured.

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

	int menu = menu_create,
			ow = window_id,
			jx,
			restore_mode,
			srf = refresh,
			gfm;

	str tstr = global_str("!VCS"),
			pkg[8] = parse_str("/VCS=", tstr),
			gfm_str = "PROMPTGET   BROWSELOCK  ";

	refresh = FALSE;

	if (pkg == "") {
		pkg = "NONE";
	}
	gfm = ((xpos(parse_str("/GFM=", tstr), gfm_str, 1) - 1) / 6) + 1;
	menu_set_item(menu, 1,
			"VCS Type:",
			pkg,
    	"/QK=1/L=1/C=1/W=8/R=10/M=DB /LT=VCS TYPES/F=VCS"
					+ "/DPT=VCS.CFG/LO=1/H=VCS^VCSTYPE/PROTECT1=NONE"
					+ "/CP=Input new VCS Type:/DT=VCS SETUP",
			8, 0, 0);

  menu_set_item(menu, 2,
			"Get Mode:",
			"PROMPT(VCS^VCS_GFMODE)GET()BROWSE()LOCK()",
			"/QK=1/L=1/C=30/W=8",
			3, gfm, 0);

  menu_set_item(menu, 3,
			"Comments:",
			"",
			"/QK=1/L=3/C=1/H=VCS^OPTS_MLC",
			10, 0, 0);

  menu_set_item(menu, 4,
			"Multi-line.  ",
			"",
      "/L=3/C=11/MIN=1/H=VCS^OPTS_MLC",
			12, parse_int('/MLC=', tstr) != 0, 0);

  menu_set_item(menu, 5,
			"Single line. ",
			"",
      "/L=4/C=11/H=VCS^OPTS_MLC",
			12, parse_int('/MLC=', tstr) == 0, 0);

	menu_set_item(menu, 6,
			"Options:",
			"",
			"/L=3/C=30",
			10, 0, 0);

	menu_set_item(menu, 7,
			"Show Cmdline   ",
			"",
			"/QK=1/L=3/C=39/H=VCS^OPTS_SHOW",
			13, parse_int('/SCL=', tstr), 0);

	menu_set_item(menu, 8,
			"No Prompts     ",
			"",
			"/QK=1/L=4/C=39/H=VCS^OPTS_NO_PROMPT",
			13, parse_int('/SNP=', tstr), 0);

	menu_set_item(menu, 9,
			"Display Window ",
			"",
			"/QK=1/L=5/C=39/H=VCS^OPTS_DISP_WIN",
			13, parse_int('/DW=', tstr) == 3, 0);

  menu_set_item(menu, 10,
			"Browse Saved   ",
			"",
			"/QK=1/L=6/C=39/H=VCS^OPTS_BSAV",
      13, parse_int('/BS=', tstr), 0);

  menu_set_item(menu, 11,
			"Use Current DIR",
			"",
			"/QK=1/L=7/C=39/H=VCS^OPTS_CDIR",
      13, parse_int('/UCD=', tstr), 0);

  menu_set_item(menu, 12,
			"Exit Status    ",
			"",
			"/QK=1/L=8/C=39/H=VCS^OPTS_XIT_STAT",
      13, parse_int('/XIT=', tstr), 0);

  menu_set_item(menu, 13,
			"File Restored  ",
			"",
			"/QK=1/L=9/C=39/H=VCS^OPTS_FRELOAD",
      13, parse_int("/FRL=", tstr), 0);

  if (parse_int('RVC=', global_str('!VCSID'))) {
  	menu_set_item(menu, 14,
				"Color Diffs    ",
        "",
        "/QK=2/L=10/C=39/H=VCS^OPTS_CDIF",
        13, parse_int('/CD=', tstr), 0);
	}
  else {
  	menu_set_item(menu, 14,
				" [ ] Color Diffs ",
        "",
        "/L=10/C=39/ATTR=" + str(m_k_color),
        10, 0, 0);
	}

	restore_mode = parse_int("/VRS=", tstr);
	menu_set_item(menu, 15,
			"Restore VCS status:",
			"",
			"/QK=1/L=6/C=1/H=VCS^OPTS_RESTORE",
			10, 0, 0);

	menu_set_item(menu, 16,
			"No restore              ",
			"",
			"/C=2/L=7/H=VCS^OPTS_RESTORE",
			12, restore_mode == 0, 0);

	menu_set_item(menu, 17,
			"Status file in each dir ",
			"",
			"/C=2/L=8/H=VCS^OPTS_RESTORE",
			12, restore_mode == 1, 0);

	menu_set_item(menu, 18,
			"One global status file  ",
			"",
			"/C=2/L=9/H=VCS^OPTS_RESTORE",
			12, restore_mode == 2, 0);

	menu_set_item(menu, 19,
			"Encoded status files    ",
			"",
			"/C=2/L=10/H=VCS^OPTS_RESTORE",
			12, restore_mode == 3, 0);

  menu_set_item(menu, 20,
			"User Xlate Macros:",
			parse_str('/UXM=', global_str("!VCS")),
      "/QK=6/L=12/C=1/W=40/ML=63/H=VCS^OPTS_UXLATE",
			0, 0, 0);

  menu_set_item(menu, 21,
			" Install VCS support in STARTUP.S ",
			"VCS_Save",
      "/QK=2/L=14/C=13/R=11/M=1/H=VCS^VCSINS",
			11, 0, 0);

  menu_set_item(menu, 22,
			" add Keys to Keymap ",
			"VCS_Keys",
      "/QK=6/L=16/C=7/R=11/M=1/H=VCS^ADDKEYS",
			11, 0, 0);

  menu_set_item(menu, 23,
			" add VCS to User Menu ",
			"VCS_Add_to_User",
      "/QK=18/L=16/C=31/R=11/M=1/H=VCS^ADDUMENU",
			11, 0, 0);

	return_int = menu;

  rm("USERIN^DATA_IN /T=VCS CONFIGURATION/#=23/HN=1/H=VCS^CONFIG");

	if (return_int) {
    rm("setconfig /DB=VCS.DB/T=VCS.CFG");
		if (return_int) {
			restore_mode = 0;
      if (menu_item_int(menu, 17, 2))
				restore_mode = 1;
      else if (menu_item_int(menu, 18, 2))
				restore_mode = 2;
      else if (menu_item_int(menu, 19, 2))
				restore_mode = 3;

			gfm = ((menu_item_int(menu, 2, 2) - 1) * 6) + 1;
			tstr = shorten_str(copy(gfm_str, gfm, 6));
			put_line("\fVCS.CFG /VCS=" + menu_item_str(menu, 1, 2)
					+ "/GFM=" + tstr
      		+ "/MLC=" + str(menu_item_int(menu, 4, 2))
					+ "/SCL=" + str(menu_item_int(menu, 7, 2))
					+ "/SNP=" + str(menu_item_int(menu, 8, 2))
      		+ "/DW=" + str(3 * menu_item_int(menu, 9, 2))
      		+ "/BS=" + str(menu_item_int(menu, 10, 2))
      		+ "/UCD=" + str(menu_item_int(menu, 11, 2))
      		+ "/XIT=" + str(menu_item_int(menu, 12, 2))
      		+ "/FRL=" + str(menu_item_int(menu, 13, 2))
      		+ "/CD=" + str(menu_item_int(menu, 14, 2))
					+ "/VRS=" + str(restore_mode)
					+ "/UXM=" + menu_item_str(menu, 20, 2));

      down;
      forward_till('^');
      right;
      rm('delEOL');
      return_str = get_line + 'VIM_SETUP_' + caps(menu_item_str(menu, 1, 2));
      put_line(return_str);
      set_global_str("!VCS", "");
		}
    rm("vcs_init");
		switch_win_id(ow);
	}
	menu_delete(menu);
	refresh = srf;
}  // VCS_Configure

macro VCS_Save trans {
/******************************************************************************
															 Multi-Edit Macro
															 27-Jul-93  17:39

	Macro   : VCS_Save
	Function: Saves and compiles the changes to the user's STARTUP.S file
						for the selected VCS type.
  Syntax  : rm('VCS_Save');
	Entry   : None.  (Called from VCS_Configure).
	Exit    : Changes written to STARTUP.S and then compiled.

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

	str file_spec,
			e_f_name;

	int sr = reg_exp_stat,
			Old_Win = window_id,
			Tmp_Win,
			Err_Win;

	str	gfm = global_str('!VCSGFM'),
			usr_path = getUserPath(0),
			tstr;

  reg_exp_stat = FALSE;
	// Mod to handle correct placement of .TMP files
	rm("TMP_FILE_NAME /FN=SETER.TMP");
	e_f_name = return_str;

	switch_window(window_count);
	create_window;
	tmp_win = window_id;
	// Mod to handle correct placement of .TMP files
	rm("TMP_FILE_NAME /FN=!!VCS!!.TMP");
	file_name = return_str;
	file_changed = 0;
	set_global_str('!VCSGFM', 'GET');

	return_str = ".;" + usr_path + ";" + usr_path + 'SRC\';
	if (usr_path != me_path) {
		return_str += ";" + me_path + 'SRC\';
	}
	error_level = 1;
	rm("pathSearch /F=STARTUP.S");
	if (return_int) {
		if ((usr_path != me_path) && (return_str == (me_path + 'SRC\STARTUP.S'))) {
			if (file_exists(usr_path + 'SRC') == 2) {
				tstr = usr_path + 'SRC\STARTUP.S';
			}
			else {
				tstr = usr_path + 'STARTUP.S';
			}
			if (!copy_file(return_str, tstr, FALSE))
				return_str = tstr;
		}
		error_level = 0;
  	rm('ldfiles');
	}
	if (error_level != 0) {
    rm('MEERROR^MessageBox /B=2/M=Unable to locate STARTUP.S. Installation not complete.');
		error_level = 0;
		goto exit;
	}

	tof;
	if (search_fwd('VCS Setup', 0)) {
		make_message("VCS Support already installed in STARTUP.S.");
		goto exit;
	}
	if (read_only) {
		rm("MeError^MessageBox /B=2/M=STARTUP.S is readonly. Aborting Install.");
		goto exit;
	}
	eof;
  if (search_bwd('}', 0)) {
		goto_col(1);
		set_indent_level;
		cr;
		up;
		Put_Line('//****** VCS Setup ******');
		down;
    put_line('|9rm("VCS^VCS_Restore");');
		down;
		Put_Line('//***********************');
		down;
		down;
    put_line('}  // Startup');
	}
	else {
		make_message('Corrupt STARTUP.S.');
		goto Exit;
	}

	save_file;

	return_int = TRUE;
	del_file(e_f_name);
	error_level = 0;
	return_str = me_path + 'CMAC -P' + getUserMacroPath + ' ' + file_name;

  rm('MEUTIL1^EXEC /RED=' + e_f_name);
	if (error_level !=  0) {
    rm('MEERROR^MessageBox /B=2/M=Error occurred compiling STARTUP.S.');
		return_int = FALSE;
		goto Exit;
	}
	switch_window(Window_Count);
	create_window;
	err_win = window_id;
	load_file(e_f_name);
	return_int = 1;
	if (error_level == 0) {
		if (search_fwd('ERROR',0)) {
      rm('MEERROR^MessageBox /B=2/M=Error occurred compiling STARTUP.S.');
			return_int = FALSE;
			goto exit;
		}
	}
	del_file(e_f_name);
	if (Switch_win_id(Err_Win))
		delete_window;
	make_message('VCS support installed in STARTUP.S');

Exit:
	set_global_str('!VCSGFM', gfm);
	if (switch_win_id(tmp_win))
		delete_window;
	switch_win_id(old_win);
	reg_exp_stat = sr;
	redraw;
}  // VCS_Save

macro VCS_Keys trans {
/******************************************************************************
															 Multi-Edit Macro
															 24-Jul-93  02:38

  Macro   : VCS_Keys
  Function: Adds the VCS quick keys to the Keymap.
  Syntax  : rm("VCS_Keys");
  Entry   : None.  (Called from VCS_Configure).
  Exit    : None.

								 Copyright (C) 1993 by Small Systems Support.
***********************************************************************(ldh)**/

	int ow = window_id,
			srf = refresh,
			sre = reg_exp_stat,
			key_win,
			vcs_win,
			vcs_line,
			jx,
			keyval = parse_int("/K=", mparm_str)
			;

	str kstr[10],
			tstr,
			mc,
			param,
			desc[34],
			fkl[6],
			mf[8],
			mid[20],
			fword
			;

	refresh = FALSE;
	return_str = parse_str("FN=", global_str("@KEYMAP_NAME@"));
	if (global_str("@DB_EXTENSION") == "") {
		return_str = return_str + ".DB";
	}
	else {
		return_str = return_str + "." + global_str("@DB_EXTENSION");
	}

	rm('MakeUserPath /DF=1');
	if (switch_file(return_str) == 0) {
		switch_window(window_count);
		create_window;
		error_level = 0;
		load_file(return_str);
		if (error_level) {
			delete_window;
			rm("MEERROR^MESSAGEBOX /B=2/T=ERROR " + str(error_level) +
														"/M=Can't find " + return_str + ".");
			error_level = 0;
			goto exit;
		}
	}
	key_win = window_id;
	window_attr = 0x81;
	tof;
	reg_exp_stat = TRUE;
	if (search_fwd("@@þ=-+ VCS SPECIFIC KEYMAP -", 0)) {
		make_message("VCS keys are already in your keymap.");
		goto exit;
	}
	eof;
	if (c_col > 1)
			down;

  rm("setconfig /DB=VCS.DB/T=VCS.MNU");
	if (!return_int) {
		rm("MEError^MessageBox /B=2/M=The VCS Menu can't be found");
		goto exit;
	}
	vcs_win = cur_window;
	vcs_line = c_line;
	switch_win_id(key_win);

	if (!(return_int = keyval)) {
		rm("userin^xmenu /B=1/L=VCS Keymap Insert/T=1/M=1 - Insert assignments on <F11>(VCS^CONFIG)2 - Insert assignments on <F12>()3 - Insert blank assignments()");
	}
	if( return_int ) {
		if( return_int == 1 )
			kstr = "F11";
		else if ( return_int == 2 )
			kstr = "F12";
		else
			kstr = "";

		keyval = return_int;
		put_line(
"@þ=-------------------------- VCS SPECIFIC KEYMAP ---------------------------");
		down;
		return_str = "";
		call make_kstr;
		put_line("MC=VCSDESCR=VCS - Version Control Support" +
					 	"K1=" + return_str + "FKL=VCSMF=VCSMID=!VCS_KeyMODE=1");
		do {
			tstr = get_line_from_win(++vcs_line, vcs_win);
			if (xpos("\f", tstr, 1) || (tstr == "")) {
				break;
			}
			if (parse_int("TYPE=", tstr) == 1) {
				desc = copy(parse_str("NAME=", tstr), 1, 34);
				mid = parse_str("QKID=", tstr);
				mc = parse_str("CMD=", tstr);
				mf = "";
				param = "";
				fkl = "";
				if (jx = xpos("^", mc, 1)) {
					mf = copy(mc, 1, jx - 1);
					mc = copy(mc, jx + 1, svl(mc) - jx);
				}
				if (jx = xpos(" ", mc, 1)) {
					param = copy(mc, jx + 1, svl(mc) - jx);
					mc = copy(mc, 1, jx - 1);
				}
				down;

				fword = copy(desc, 1, xpos(" ", desc, 1) - 1);
				return_str = "";
				switch (fword) {
					case "Directory":
						return_str = "Shft";
						fkl = "VCSDir";
						call make_kstr;
						break;

					case "Extract":
						return_str = "Ctrl";
						fkl = "VCSGet";
						call make_kstr;
						break;

					case "History":
						fkl = "VCSLst";
						break;

					case "Multi-Edit":
						fkl = "VCSOut";
						break;

					case "Put":
						return_str = "Alt";
						fkl = "VCSPut";
						call make_kstr;
						break;

					case "Refresh":
						fkl = "VCSBrw";
						break;

					case "Unlock":
						fkl = "VCSUnL";
						break;

					case "Visual":
						fkl = "VCSCmp";
						break;

					case "Configure...":
						fkl = "VCSCfg";
						break;

					case "ruN":
						fkl = "VCSRun";
						break;

					case "SourceSafe":
						fkl = "SSUser";
						break;
				}
				put_line("MC=" + mc + "PARAM=" + param + "DESCR=" + desc +
								 "K1=" + return_str + "FKL=" + fkl + "MF=" + mf +
								 "MID=" + mid + "MODE=1");
			}
		} while (TRUE);
    set_global_int("SETUP_CHANGED", global_int("SETUP_CHANGED") | 0x02);
    make_message("VCS default key assignments created.");
		if (file_changed) {
			save_file;
		}
		delete_window;
	}
  else
    make_message("VCS key assignments installation aborted.");
	goto exit;

make_kstr:
	if( kstr == "" ) {
		return_str = "";
	}
	else {
		return_str = "<" + return_str + kstr + ">";
	}
	ret;

exit:
	reg_exp_stat = sre;
	refresh = srf;
	switch_win_id(ow);
	redraw;
	return_int = keyval;

}  // VCS_Keys

macro VCS_Add_to_User trans {
/******************************************************************************
															 Multi-Edit Macro
															 23-Jul-93  22:17

  Macro   : VCS_Add_to_User
  Function: Adds the VCS Support menu to the User Menu.
  Syntax  : rm("VCS_Add_to_User");
  Entry   : None.  (Called from VCS_Configure).
  Exit    : None.

								 Copyright (C) 1993 by Small Systems Support.
***********************************************************************(ldh)**/

	int ow = window_id,
			srf = refresh,
			sre = reg_exp_stat,
			sic = ignore_case;

	refresh = FALSE;
	rm("SETCONFIG /DB=MECONFIG/T=USER.MNU");
	if (return_int) {
		eol;
		mark_pos;
		reg_exp_stat = TRUE;
		ignore_case = TRUE;

		if (search_fwd("{VCS.DB^VCS}|\f", 0)) {
			if(found_str != "\f") {
				make_message("VCS Support already in your User Menu.");
				pop_mark;
				goto exit;
			}
		}
		goto_mark;
		cr;
		put_line("NAME=VCS - Version Control Support" +
						 "CMD=VCS.DB^VCSTYPE=5QKID=!VCS_KEYHNDX=VCS^*CS=0USECMD=1");
		if (global_int("~USER_MENU")) {
			menu_delete(global_int("~USER_MENU"));
			set_global_int("~USER_MENU", 0);
		}
		make_message("VCS Support added to your User Menu.");
	}
	else {
		rm("MEERROR^messagebox /B=2/T=ERROR" +
													"/M=Unable to find User menu in MECONFIG.DB");
	}

exit:
	ignore_case = sic;
	reg_exp_stat = sre;
	refresh = srf;
	switch_win_id(ow);
	redraw;

}  // VCS_Add_to_User

macro vcs_init trans2 {
/******************************************************************************
															 Multi-Edit Macro
															 10-Dec-93  19:26

	Macro   : VCS_Init
	Function: Initializes the VCS System.
  Syntax  : rm('VCS_Init');
	Entry   : None.
	Exit    : The following global variables are setup:

							!VCS 		- Contains the VCS ID and general options.
							!VCSID 	- Contains the specific VCS options and commands.
							!VCSGFM - Contains the default Mode for the VCS_Get_File macro.
							!VCSSNP - Set to 1 to disable most prompts.

							@FNF_LOAD_MACRO@	- Contains the macro to run when the macro
																	LDFILES can not find a file.
							@ME_EXIT_MACRO@   - Contains the macro to run when exiting ME.
							@DEL_VERIFY_MACRO -	Contains the macro to run when deleting a
																	window.

                Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

	if (global_str("!VCS") == "" ) {

		str tstr,
				vcs_id,
				cmd;

		int ow = window_id,
				srf = refresh;

		refresh = FALSE;
    rm("meError^messageBox /NW=1/M=Initializing VCS...");
    rm("setConfig /DB=VCS.DB/T=VCS.CFG");
		if (return_int) {
			set_global_str("!VCS", get_line );
			tstr = global_str("!VCS");
			vcs_id = parse_str("/VCS=", tstr);
			down;
			if (find_text("^\\*\\*\\*\\*START\\*\\*\\*\\*", 0, _RegExp)) {
				down;
				while (!at_eof && (cur_char != "\f")) {
					if (parse_str("ID=", get_line) == vcs_id) {
						set_global_str("!VCSID", get_line);
						break;
					}
					down;
					goto_col(1);
				}
			}
			set_global_str("!VCSGFM", parse_str("/GFM=", tstr));
			set_global_int("!VCSSNP", parse_int("/SNP=", tstr));
			if (global_str("@FNF_LOAD_MACRO@") == "") {
				set_global_str("@FNF_LOAD_MACRO@", "vcs^vcs_get_file");
				set_global_str("@ME_EXIT_MACRO@", global_str("@ME_EXIT_MACRO@")
						+ ";vcs^vcs_cleanup");
				set_global_str("@DEL_VERIFY_MACRO", "vcs^vcs_cleanup /CF=1/ND=1/NX=1");
			}
			cmd = parse_str("GC=", global_str("!VCSID"));
			cmd = copy(cmd, 1, xpos(" ", cmd, 1) - 1);
			return_str = "<%PATH>";
			rm("pathSearch /F=" + cmd);       // Check for get command EXE file
			if (return_int) {
				set_global_str("!VCSPKGDIR", get_path(return_str));
  			if (vcs_id == "SS") {
					set_global_int("!VCS-SS", 0);
    			if (!global_int("!VCSSS")) {
    				rm("vcs_ssLogin");
					}
				}
				else {
					set_global_int("!VCS-SS", 1);
				}
				if (parse_str("MC=", global_str("!VCSID")) == "") {
					set_global_int("!VCSIACT", 1);
				}
				else {
					set_global_int("!VCSIACT", 0);
				}
				set_global_int("!VCSNONE", 0);
			}
			else {
				make_message(vcs_id + " support not available.");
				vcs_id = "NONE";
				set_global_str("!VCSID", "ID=NONE");
				set_global_int("!VCS-SS", 1);
				set_global_int("!VCSIACT", 1);
				set_global_int("!VCSNONE", 1);
			}
    	rm("setConfig /DB=VCS.DB/T=VCS.MNU");
			if (return_int) {
				forward_till("T");
				if (cur_char == "T") {
					left;
					rm("deleol");
				}
				return_str = get_line + "/T=VCS " + VCS_VERSION + " (" + vcs_id + ")";
				put_line(return_str);
				if (global_int("~VCS_MENU")) {
					menu_delete(global_int("~VCS_MENU"));
					set_global_int("~VCS_MENU", 0);
				}
 			}

			if (file_changed) {
				save_file;
			}
			if (ow != window_id) {
				delete_window;
			}
			switch_win_id(ow);
			kill_box;

			set_global_str("!VCSWRKDIR", "");
		}
		refresh = srf;
	}
}  // VCS_Init

macro VCS_Chg_Cmt trans2 {
/******************************************************************************
															 Multi-Edit Macro
															 28-Oct-91  14:01

	Macro   : VCS_Chg_Cmt
	Function:
  Syntax  : rm('VCS_Chg_Cmt');
	Entry   : None.
	Exit    :

               Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

}  // VCS_Chg_Cmt

macro vcs_ss_cmds trans2 {
/******************************************************************************
															 Multi-Edit Macro
                               29-Oct-92  00:00

  Macro   : VCS_SS_Cmds
  Function: The SourceSafe command interface.
  Syntax  : rm('VCS_SS_Cmds /C=str/F=str');
	Entry   : /F=str      The filename
						/C=str			GETDIR
 												DIR
												STATUS
												CP
                        SSLOGIN
                        FLST
            Return_Str  Contains the project name.
  Exit    : Return_Int
							FALSE - File not Found.
							TRUE  - File Found.
            Return_Str  The found project\file name or User.

                Copyright (C) 1992-93 by Small Systems Support.
*********************************************************************(ldh)** */

  int show_cmdline = FALSE,
      cmd_in_win = 0,
      snp = global_int("!VCSSNP");      // Save No Prompt global variable

  str fname[128] = parse_str("/F=", mparm_str),
      lpath = return_str,
			ss_name,
			vcs_name[128],
			cmd = caps(parse_str("/C=", mparm_str));

	// Mod to handle correct placement of .TMP files
	rm("tmp_file_name /FN=VCS_SS.TMP");
	ss_name = return_str;

	rm("tmp_file_name /FN=VCS.TMP");
	vcs_name = return_str;

  if (cmd == "FLST") {
    if(check_key) {
      push_key(key1,key2);
      goto exit;
    }
    cmd = shorten_str(get_line);
    cmd = copy(cmd, 2, svl(cmd) - 1);

    pad_str(cmd, 70, " " );
    write(cmd, win_x1 + 1, win_y2 + 1, 0, m_t_color);
  }
  else if (cmd == "GETDIR") {
    set_file_attr(ss_name, 0);
    del_file(ss_name);
    return_str = "SS.EXE DIR -I- -E -O@" + ss_name;
		call vcs_exec;
		if (return_int) {
      load_file(ss_name);
			if (error_level) {
				error_level = 0;
				return_int = FALSE;
			}
		}
    set_file_attr(ss_name, 0);
    del_file(ss_name);
  }
  else if (cmd == "SSLOGIN") {
    return_str = "SSLOGIN.EXE -E <~!VCSSSUSER> <~!VCSSSPWD>";
    call vcs_exec;
    if (return_int) {
      if (exit_code)
        return_int = FALSE;
    }
  }
  else if (cmd == "DIR") {
    set_file_attr(ss_name, 0);
    del_file(ss_name);
    return_str = "SS.EXE DIR -I- -E <LPATH><NAME>.<EXT> -O@" + ss_name;
		call vcs_exec;
		if (return_int) {
      if (exit_code) {
      	return_int = FALSE;
				fname = "";
			}
		}
		return_str = lpath + fname;
  }
  else if (cmd == "STATUS") {
    return_str = "SS.EXE STATUS -I- <LPATH><NAME>.<EXT>";
		call vcs_exec;
		if (return_int) {
      if (!exit_code) {
      	return_int = FALSE;
			}
		}
  }
  else if (cmd == "CP") {
    return_str = "SS.EXE CP -I- <LPATH>";
		call vcs_exec;
		if (return_int) {
      if (!exit_code) {
      	return_int = FALSE;
			}
		}
  }
  goto exit;

vcs_exec:
  rm("vcs_xlate /F=" + fname + "/P=" + caps(lpath));

	if (show_cmdline && !snp) {
		create_global_str('!V_ISTR_1', return_str);
		create_global_str('!V_IPARM_1', '/L=1/W=75/ML=128/H=VCS^VCS_CMDLIN');
    rm('USERIN^DATA_IN /PRE=!V_/#=1/T=SS: ' + cmd + ' Command Line');
    if (!return_int) {
      ret;
		}
		return_str = global_str('!V_ISTR_1');
		set_global_str('!V_ISTR_1', '');
	}

	working;
	return_str = lower(return_str);
  rm("meUtil1^exec /SCREEN=" + str(cmd_in_win) + "/RED=" + vcs_name
			+ "/REDERR=STDOUT");
	ret;

exit:

}  // vcs_ss_cmds

macro vcs_ss_file trans2 {
/******************************************************************************
															 Multi-Edit Macro
                               06-Mar-93  00:28

  Macro   : VCS_SS_File
  Function: Creates a file dialog box for the SourceSafe VCS package.
  Syntax  : rm('VCS_SS_File /T=str/C=str/RC=1');
	Entry   : /T=str      The title for the Dialog box.
            /C=str      The command to run.
            /RC=1       Supplied to stay in a repeat loop
  Exit    : Return_Int
							FALSE - File not Found.
							TRUE  - File Found.
            Return_Str  The found project\file name or User.

                Copyright (C) 1992-93 by Small Systems Support.
**********************************************************************(ldh)***/

	int	ow = window_id,
			menu,
			fw,
      fwn,
			pw,
      pwn,
      jx,
			fcnt,
			scnt = 3,
			bc = box_count,
			si = insert_mode,
      sundo = undo_stat,
      sr = reg_exp_stat,
			use_cd = parse_int("/UCD=", global_str("!VCS")),
      rep = parse_int("/RC=", mparm_str),
			srf = refresh;

  str cmd = parse_str("/C=", mparm_str),
      tstr,
			wrk_dir,
			prj_name;

	undo_stat = FALSE;
	insert_mode = TRUE;
	refresh = FALSE;

  switch_window(window_count);
  create_window;
  screen_num = 0;
  fw = window_id;

get_dir:
  fwn = cur_window;
  rm("vcs_ss_cmds /C=GETDIR");
  if (!return_int) {
    goto err_exit;
	}
  file_name = fexpand("ss_file!.dir");
  switch_window(window_count);
  create_window;
  screen_num = 0;
  pw = window_id;
  pwn = cur_window;
  file_name = fexpand("ss_prj!.dir");

  switch_win_id(fw);
  tof;
  prj_name = get_word(" ");
  jx = xpos("/", prj_name, 1);
  while (jx > 0) {
    prj_name = copy(prj_name, 1, jx - 1) + "\\" + copy(prj_name, jx + 1, 255);
    jx = xpos("/", prj_name, 1);
  }
  del_line;
  return_str = str_del(prj_name, 1, 2);
  fcnt = 0;
  if (return_str != ":") {
    ++fcnt;
    put_line_to_win("..\\", fcnt, pwn, FALSE);
  }
  prj_name = copy(prj_name, 1, svl(prj_name) - 1);
  if (copy(prj_name, svl(prj_name), 1) != "\\") {
    prj_name = prj_name + "\\";
	}
  while (1) {
    goto_col(1);
    if (cur_char == "$") {
      ++fcnt;
      return_str = str_del(get_word(" "), 1, 1) + "\\";
      put_line_to_win(caps(return_str), fcnt, pwn, FALSE);
      del_line;
    }
    else {
      break;
		}
  }
  down;
  eol;
  if (at_eof) {
    scnt = 4;
	}
  else {
//     scnt = 3;
    goto_col(1);
    up;
    do {
      text(" ");
      down;
      goto_col(1);
    } while (!at_eof);
  }
  up;
  block_begin;
  eof;
  delete_block;
  tof;

dir_loop:
	menu = menu_create;
  switch_win_id(fw);

	if (use_cd) {
  	menu_set_item(menu, 1,
				"WrkDir: " + dir_path,
				"",
				"/C=1/ATTR=" + str(m_k_color),
				10, 0, 0);
	}
	else {
		set_global_str('VCS_WRK_HISTORY0', dir_path);
		tstr = global_str("VCS_WRK_HISTORY"
				+ str(parse_int("/#=", global_str("VCS_WRK_HISTORY"))));
  	menu_set_item(menu, 1,
				"WrkDir:",
				tstr,
				"/QK=1/C=1/W=64/ML=80/HISTORY=VCS_WRK_HISTORY/NHA="
						+ parse_str("/NHA=", mparm_str),
				0, 0, 0);
	}

	menu_set_item(menu, 2,
			"Current Project: " + prj_name + "*.*",
			"",
			"L=2/C=1",
			10, 0, 0);

	menu_set_item(menu, 3,
			"Files",
			"",
      "/OL=1/QK=1/DC=1/L=4/C=1/W=39/HT=15/MRK=1/WIN=" + str(fwn) +
      "/DISPMAC=VCS_SS_Cmds //C=FLST",
			15, 0, 0);

	menu_set_item(menu, 4,
			"Projects",
			"",
      "/OL=1/QK=1/DC=1/L=4/C=43/W=14/HT=15/WIN=" + str(pwn),
			15, 0, 0);

  menu_set_item(menu, 5,
			"OK",
			"",
			"/KC=<ENTER>/C=60/L=2/K1=13/K2=28/R=1",
			11, 0, 0);

  menu_set_item(menu, 6,
			"Cancel",
			"",
			"/KC=<ESC>/C=60/L=4/K1=27/K2=1/R=0",
			11, 0, 0);

  menu_set_item(menu, 7,
			"Help",
			"",
			"/KC=<F1>/C=60/L=6/K1=0/K2=59/QK=1/R=2",
			11, 0, 0);

  menu_set_item(menu, 8,
			"Get (Browse)",
			"",
			"/C=60/L=8/QK=1/R=13",
			11, 0, 0);

  menu_set_item(menu, 9,
			"Update (Put)",
			"",
			"/C=60/L=10/QK=1/R=15",
			11, 0, 0);

  menu_set_item(menu, 10,
			"Lock (Reserve)",
			"",
			"/C=60/L=12/QK=1/R=14",
			11, 0, 0);

  menu_set_item(menu, 11,
			"History (List)",
			"",
			"/C=60/L=14/QK=1/R=12",
			11, 0, 0);

  menu_set_item(menu, 12,
			"Checkout (Get)",
			"",
			"/C=60/L=16/QK=1/R=11",
			11, 0, 0);

  menu_set_item(menu, 13,
			"Uncheck (Unlock)",
			"",
			"/C=60/L=18/QK=2/R=16",
			11, 0, 0);

  menu_set_item(menu, 14,
			"",
			"",
			"L=20",
			10, 0, 0);

	return_int = menu;
  rm("userIn^data_in /NOW=1/ABT=OK/HN=1/#=14/A=3/H=VCS^SS_FILE/S="
			+ str(scnt) + "/T=" + parse_str("/T=", mparm_str) + " - " + cmd);
  if (return_int) {
  	wrk_dir = menu_item_str(menu, 1, 2);
  	if ((svl(wrk_dir) > 0) && (copy(wrk_dir, svl(wrk_dir), 1) != "\\")) {
    	wrk_dir = wrk_dir + "\\";
		}
    return_str = global_str("!V_ISTR_1");
    if (window_id == pw) {
			scnt = 4;                         // keep project list selected
      if (return_int == 1) {
        return_str = get_line;
        call cleanup;
        switch_win_id(fw);
        rm("vcs_ss_cmds /C=CP");
				rm("vcs_get_ssprj");            // get current project
        goto get_dir;
      }
      else {
        call cleanmenu;
        goto dir_loop;
      }
		}
    else if (window_id == fw) {
			scnt = 3;                         // keep file list selected
      call cleanmenu;
      switch (return_int) {
        case 11:
          cmd = "GET";
          break;
        case 12:
          cmd = "LIST";
          break;
        case 13:
          cmd = "BROWSE";
          break;
        case 14:
          cmd = "LOCK";
          break;
        case 15:
          cmd = "PUT";
          break;
        case 16:
          cmd = "UNLOCK";
          break;
      }
      mark_pos;
      tof;
      reg_exp_stat = TRUE;
      if (search_fwd('%|175', 0)) {
        do {
          call do_cmds;
          eol;
        } while (search_fwd('%|175', 0));
        goto_col(1);
      }
      else {
        goto_mark;
        call do_cmds;
      }
      if (rep) {
        goto dir_loop;
			}
		}
  }
	goto exit;

do_cmds:
      goto_col(2);
			return_str = prj_name + get_word(" ");
      tstr = return_str;
      forward_till_not(" \t\xFF");
      set_global_str("!VCSSS_FUSR", get_word(" "));
  		set_global_str("!VCSWRKDIR", wrk_dir);
      rm("vcs_cmds /DF=1/C=" + cmd + "/P=" + return_str);
      if (return_int) {
        if ((cmd == "UNLOCK") || (cmd == "PUT")) {
          switch_win_id(pw);
          pwn = cur_window;
          switch_win_id(fw);
          fwn = cur_window;
          tof;
          tstr = truncate_path(tstr);
          if (search_fwd(tstr, 0)) {
            forward_till(" ");
            rm("delEOL");
          }
        }
        else  {
					ow = window_id;
        	if ((cmd == "GET") || (cmd == "LOCK")) {
          	tstr = truncate_path(tstr);
          	switch_win_id(fw);
          	tof;
          	if (search_fwd(tstr, 0)) {
            	goto_col(16);
            	text(global_str("!VCSSSUSER"));
            	goto_col(30);
            	text(copy(date, 1, 2) + "/" + copy(date, 4, 2) + "/" + copy(date, 7, 2));
            	goto_col(40);
            	text(str_del(str_del(time, 6, 3), 7,1));
            	goto_col(48);
            	tstr = get_path(fexpand(tstr));
            	tstr = copy(tstr, 1, svl(tstr) - 1);
            	text(tstr);
          	}
          	goto_col(1);
        	}
				}
      }
      ret;

cleanup:
	set_global_str("!VCSSSF", "");
  switch_win_id(pw);
	delete_window;

cleanmenu:
	while (box_count > bc)
	{
		kill_box;
	}
	menu_delete(menu);
	ret;

exit:
	call cleanup;

err_exit:
  switch_win_id(fw);
	delete_window;

done:
  set_global_str("!V_ISTR_1", "");
	set_global_str("VCS_WRK_HISTORY0", "");
	refresh = srf;
	insert_mode = si;
  undo_stat = sundo;
  reg_exp_stat = sr;
  switch_win_id(ow);

}  // vcs_ss_file

macro vcs_sslogin trans {
/******************************************************************************
															 Multi-Edit Macro
                               09-Oct-92  14:57

  Macro   : VCS_SSLogin
  Function: Check if a SourceSafe user is logged in, and prompts if not.
  Syntax  : rm('VCS_SSLogin');
  Entry   : None.
  Exit    : Return_Int
              FALSE - Error checking user logged in.
              TRUE  - User logged in.
            Global Variable '!VCSSSUSER' is set to SourceSafe user name.
                            '!VCSNOSS' is set if no user logged in.

                Copyright (C) 1992-93 by Small Systems Support.
*********************************************************************(ldh)** */

  int ow = window_id,
      menu = menu_create;

  str ssuser[128],
			ssprj[128],
			vcs_name[128];

	// Mod to handle correct placement of .TMP files
	rm("tmp_file_name /FN=VCS.TMP");
	VCS_name = return_str;

  switch_window(window_count);
  create_window;

  return_int = !parse_int("/CU=", mparm_str);     // Change User
  if (return_int) {
    rm("meError^messageBox /NW=1/M=Checking SourceSafe Login Status...");
    set_global_str("!VCSSSUSER", "");
    set_global_int("!VCSNOSS", 1);
    rm("vcs_ss_cmds /C=SSLOGIN");
    kill_box;
  }
  if (!return_int) {
    while (TRUE) {
      ssuser = get_environment("SSUSER");
      menu_set_item(menu, 1,
					"Username: ",
					ssuser,
					"/QK=1/C=1/L=2/W=20",
        	0, 0, 0);

      menu_set_item(menu, 2,
					"Password: ",
					"",
					"/QK=1/C=1/L=4/W=20/HID=1",
        	0, 0, 0);

      return_int = menu;
      rm("userIn^data_in /T=SourceSafe Login/S=1/#=2/HN=1"
					+ "/H=VCS^SS_LOGIN_DIALOG");
      if (return_int) {
        set_global_str("!VCSSSUSER", menu_item_str(menu, 1, 2));
        set_global_str("!VCSSSPWD", menu_item_str(menu, 2, 2));
        rm("meError^messageBox /NW=1/M=Logging into SourceSafe...");
        rm("vcs_ss_cmds /C=SSLOGIN");
        if (return_int) {
          set_global_str("!VCSSSUSER", "");
          set_global_str("!VCSSSPWD", "");
          rm("VCS_SS_Cmds /C=SSLOGIN");
          kill_box;
          if (return_int) {
            break;
					}
          goto err_exit;
        }
      }
      kill_box;
      rm("userIn^verify /H=VCS^SS_LOGIN_DIALOG/T=Invalid User or Password."
					+ "/BL= TRY AGAIN? ");
      if (!return_int) {

err_exit:
        rm("messageBox /T=SourceSafe Login Error/M=\"SSINIT\" not found,"
						+ " SS Support not Loaded");
        goto exit;
      }
    }
  }
  load_file(vcs_name);
  if (error_level) {
    error_level = 0;
    goto exit;
  }
  if (find_text("User: ", 0, 0)) {
		ssuser = shorten_str(get_line);
		ssuser = copy(ssuser, length(found_str) + 1, 255);
    set_global_str("!VCSSSUSER", ssuser);
    set_global_int("!VCSNOSS", 0);
  }
	if (find_text("Project: ", 0, 0)) {
		ssprj = shorten_str(get_line);
		ssprj = copy(ssprj, length(found_str) + 1, 255);
		set_global_str("!VCSSSPRJ", ssprj);
	}
	if (find_text("Ini file: ", 0, 0)) {
		ssprj = shorten_str(get_line);
		ssprj = copy(ssprj, length(found_str) + 1, 255);
		set_global_str("!VCSSSINI", ssprj);
	}

exit:
  set_global_int("!VCSSS", 1);
  menu_delete(menu);
  delete_window;
  del_file(vcs_name);
  switch_win_id(ow);

}  // vcs_sslogin

macro vcs_get_ssprj trans2 {
/******************************************************************************
															 Multi-Edit Macro
                               21-Dec-93  00:53

  Macro   : vcs_get_ssprj
  Function: Reads the user's SS.INI file for the current project.
  Syntax  : rm("vcs_get_ssprj");
  Entry   : None.
  Exit    : The current users current SourceSafe project is store in the
						global_str !VCSSSPRJ

								 Copyright (C) 1993 by Small Systems Support.
**********************************************************************(ldh)***/

	int ow = window_id,
			srf = refresh,
			jx;

	str	prj,
			fname = global_str("!VCSSSINI");

	refresh = FALSE;
	set_global_str("!VCSSSPRJ", "");
	switch_window(window_count);
	create_window;
	load_file(fname);
	if (!error_level) {
		if (find_text("[Internal]", 0, 0)) {
			down;
			forward_till("$");
			prj = get_word("");
			jx = 1;
			while (jx = xpos("/", prj, 1)) {
				prj = str_del(prj, jx, 1);
				prj = str_ins("\\", prj, jx);
			}
			set_global_str("!VCSSSPRJ", prj);
		}
	}
	error_level = 0;
	delete_window;
	switch_win_id(ow);
	refresh = srf;
	make_message(prj);
}  // vcs_get_ssprj

macro vcs_file trans2 {
/******************************************************************************
															 Multi-Edit Macro
                               05-Mar-93  23:01

  Macro   : VCS_File
	Function: Creates a FILE dialog box, complete with directory window.
  Syntax  : rm('VCS_File /T=str/NHA=1/C=str');
	Entry   : /T=str	Dialog box title.
						/NHA=1 	No History Add.  Use this parameter if you want
										additions to the file history list to be done outside
										of this prompt.
            /C=str  VCS command to execute.

            Return_Str = default file name.

  Exit    : Return_Int > 0	Function selected.
              11 - GET
              12 - LIST
              13 - BROWSE
              14 - LOCK

            Return_Str  The found project\file name or User.

                Copyright (C) 1992-93 by Small Systems Support.
*********************************************************************(ldh)** */

	int menu = menu_create,
			ow = window_id,
			use_cd = parse_int('/UCD=', global_str('!VCS')),
      menu_level = global_int('MENU_LEVEL');

  str cmd = parse_str('/C=', mparm_str),
			tmp_str;

	refresh = false;
	switch_window(window_count);
	create_window;

	if (use_cd) {
  	menu_set_item(menu, 1, "WrkDir: " + dir_path,
													 "",
													 "/C=1/ATTR=" + str(m_k_color),
													 10, 0, 0);
	}
	else {
		set_global_str('VCS_WRK_HISTORY0', dir_path);
		tmp_str = global_str("VCS_WRK_HISTORY" +
								str(parse_int("/#=", global_str("VCS_WRK_HISTORY"))));
  	menu_set_item(menu, 1, "WrkDir:", tmp_str,
													 "/QK=1/C=1/W=51/ML=80/HISTORY=VCS_WRK_HISTORY/NHA="
													 + parse_str("/NHA=", mparm_str),
													 0, 0, 0);
	}

	set_global_str('VCS_HISTORY0', return_str);
	tmp_str = global_str("VCS_HISTORY" +
								str(parse_int("/#=", global_str("VCS_HISTORY"))));

  menu_set_item(menu, 2, 'Name:  ', tmp_str,
			'/QK=1/C=1/W=51/ML=80/HISTORY=VCS_HISTORY/NHA='
			+ parse_str('/NHA=', mparm_str), 0, 0, 0);

	menu_set_item(menu, 3, '', '', '/C=1/L=4/SDD=4/MSK=2',20, 0, 0);
	menu_set_item(menu, 4, 'Directories', '', '/OL=1/DC=1/QK=1/C=34/L=4/W=14/HT=14/WIN=' + str(cur_window),15,0, 0);
  menu_set_item(menu, 5, 'OK','', '/KC=<ENTER>/C=51/L=4/K1=13/K2=28/R=1',11,0, 0);
  menu_set_item(menu, 6, 'Cancel','', '/KC=<ESC>/C=51/L=6/K1=27/K2=1/R=0',11,0, 0);
  menu_set_item(menu, 7, 'Help','','/KC=<F1>/C=51/L=8/K1=0/K2=59/R=2/QK=1',11,0, 0);
  menu_set_item(menu, 8, 'Sort', 'DIRSHELL^DIRSORT /UPDATE=5','/R=1/C=51/L=10/M=1/QK=1',11, 0, 0);
  menu_set_item(menu, 9, 'Get','','/C=51/L=12/R=11/QK=1', 11, 0, 0);
  menu_set_item(menu, 10, 'List','','/C=51/L=14/R=12/QK=1', 11, 0, 0);
  menu_set_item(menu, 11, 'Browse','','/C=51/L=16/R=13/QK=1', 11, 0, 0);
  menu_set_item(menu, 12, 'Reserve','','/C=51/L=18/R=14/QK=1', 11, 0, 0);

  return_int = menu;
  rm('UserIn^Data_In /HN=1/T=' + parse_str('/T=', mparm_str) + ' - ' + cmd +
			'/S=3/#=12/PRE=' + str(menu_level) + '/POSG=@FPPOS@/H=VCS^DIR/WM=1');
	return_str = menu_item_str(menu, 2, 2);
  cmd = menu_item_str(menu, 1, 2);
  if ((svl(cmd) > 0) && (copy(cmd, svl(cmd), 1) != '\'))
    cmd = cmd + '\';
  set_global_str('!VCSWRKDIR', cmd);
	set_global_str('VCS_WRK_HISTORY0', '');
	set_global_str('VCS_HISTORY0', '');
	menu_delete(menu);
	delete_window;
	switch_win_id(ow);
}  // VCS_File

macro VCS_IActive trans {
/******************************************************************************
															 Multi-Edit Macro
															 04-Mar-93  00:43

	Macro   : VCS_IActive
	Function: Runs the selected VCS package menu program.
  Syntax  : rm('VCS_IActive');
	Entry   : None.
	Exit    : None.

               Copyright (C) 1990-93 by Small Systems Support.
*********************************************************************(ldh)** */

	return_str = parse_str('MC=', global_str('!VCSID'));
  rm('MEUTIL1^EXEC /SCREEN=1');
}  // VCS_IActive

macro VCS_XMacro trans {
/******************************************************************************
															 Multi-Edit Macro
                               12-May-93  23:10

	Macro   : VCS_XMacro
	Function: Translates user defined aliases. <PRJ> and <SUBSYS> are expanded
 						out.
  Syntax  : rm('VCS_XMacro /F=str/P=str');
  Entry   : /F=str  The file name to use.
            /P=str  The path to use for <LPATH>.
	Exit    : Return_Str  The expanded command line.
	Calls   : None

								 Copyright (C) 1993 by Small Systems Support.
**********************************************************************(ldh)***/

/*
  If the /F=filename is as follows  D:\PRJ\SUBSYS\FILENAME.EXT the translation
  will be as follows.

  <PRJ> = \PRJ\
  <SUBSYS> = \SUBSYS\

  Now if the Libary Path is as follows  Y:\PROJ<PRJ>REPO<SUBSYS> it will be
  translated to the following.

  Y:\PROJ\PRJ\REPO\SUBSYS\.
*/

	str fname[128], lpath[128], t_path;
	int jx, offset;

	fname = Parse_Str('/F=', MParm_Str);
	lpath = Parse_Str('/P=', MParm_Str);
	if (lpath == '')
		lpath = Get_Path(fname);

  while (jx = xpos('<PRJ>', caps(return_str), 1)) {
    return_str = str_del(return_str, jx, 5);
    t_path = get_path(fname);
    if ((svl(t_path) > 2) && (copy(t_path, 2, 1) == ':'))
      t_path = str_del(t_path, 1, 2);
		offset = xpos('\', t_path, 2);
		t_path = str_del(t_path, offset + 1, svl(t_path) - offset);
    return_str = str_ins(t_path, return_str, jx);
	}

  while (jx = xpos('<SUBSYS>', caps(return_str), 1)) {
    return_str = str_del(return_str, jx, 8);
    t_path = get_path(fname);
    if ((svl(t_path) > 2) && (copy(t_path, 2, 1) == ':'))
      t_path = str_del(t_path, 1, 2);
		t_path = str_del(t_path, 1, xpos('\', t_path, 2) - 1);
    return_str = str_ins(t_path, return_str, jx);
	}
}  // VCS_XMacro

/*
macro VCS_Test trans {
	return_str = "Y:\\PROJ<PRJ>REPO<SUBSYS>";
 	rm("VCS_Xlate /F=" + file_name);
	make_message(return_str);
}
*/
