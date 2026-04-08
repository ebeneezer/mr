//+++keyword+++ "@(#)%v %n, %f, %w"
// "@(#)4 VCS_INST.S, 29-Oct-93,15:49:04, LDH"

macro_file VCS_INST;

#define MAXPKG 7

#define NONE	0x00
#define PVCS 	0x01
#define RCS  	0x02
#define SAP		0x04
#define SS		0x08
#define TLIB4	0x10
#define TLIB5	0x20

prototype vcs_inst
{
	void replace_parm(str &src, fnd, repl);
}

macro vcs_inst dump {

  int menu = menu_create,
			mcnt = 0,
			ins_type = 0,
			old_menu = 0,
			vcs_pkgs = 0,
			pkg_avail = 0,
			def_pkg = 0,
			master = 0,
			all_users = 0,
      net = (user_id != "") && (get_environment("ME_CFG_DIR") == ""), // tmj
      height = (screen_length - 5),
      org_win = window_id,
			vcs_ref_win = 0,
      vcs_db_win,
			wrk_win = 0,
			cur_pkg,
			userid_only = FALSE,
      srf = refresh,
			sre = reg_exp_stat,
			sic = ignore_case,
			sim = insert_mode,
			pkg_cnt,
			vcs_dsrc,
			keyval = 0
			;

  str old_path,
      usr_vcs_db,
      vcs_dat = me_path + "VCS.DAT",
      suser_id = user_id,
			suser_path = user_path,
			menu_str[8],
			pkg_str = "NONE    PVCS    RCS     SAP     SS      TLIB4   TLIB5   ",
			parm_str,
			tstr[2048],
			tline[2048],
			vcs_userid[20],
			vcs_ext[3],
			vcs_lok[3],
      vcs_path,
      vcs_message = "VCS Installation terminated by user."
      ;

  load_macro_file("VCS");         // TMJ
  if (!file_exists(vcs_dat)) {          // check for VCS.DAT
    beep;
    rm("meHelp /X=10/Y=3/W=60/LK=VCS_INST^NODAT/L=" + str(height));
    goto exit;
  }

  refresh = FALSE;
	reg_exp_stat = FALSE;
	ignore_case = TRUE;
	if (!switch_file(vcs_dat)) {
		create_window;
		vcs_db_win = window_id;
		file_name = vcs_dat;
		call do_load_file;
	}
	call set_pkgs;
	pkg_avail = vcs_pkgs;

  return_str = "VCS.DB";
  rm("makeUserPath");
  usr_vcs_db = return_str;
  if (file_exists(usr_vcs_db)) {
    ins_type = TRUE;                    // update
    if (switch_file(usr_vcs_db)) {
			vcs_ref_win = window_id;
      if (file_changed) {
        save_file;
      }
    }
		else {
			create_window;
			vcs_ref_win = window_id;
			file_name = usr_vcs_db;
			call do_load_file;
		}
		call set_pkgs;
		call get_dflt;
		erase_window;
  }

  rm("mehelp /IM=1/X=10/Y=3/W=60/LK=VCS_INST^START/L=" + str(height));

  if (return_int) {                     // Do Installation
    menu_set_item(menu, ++mcnt,         // 1
                  "Type:",
                  "",
                  "/L=2/C=2/H=",
                  10, 0, 0);

    menu_set_item(menu, ++mcnt,         // 2
                  "New       ",
                  "",
                  "/QK=1/L=3/C=3/H=",
                  12, ins_type == 0, 0);

    menu_set_item(menu, ++mcnt,         // 3
                  "Update    ",
                  "",
                  "/QK=1/L=4/C=3/H=",
                  12, ins_type == 1, 0);

    menu_set_item(menu, ++mcnt,         // 4
                  "Old Menus:",
                  "",
                  "/L=6/C=2/H=",
                  10, 0, 0);

    menu_set_item(menu, ++mcnt,         // 5
                  "Save      ",
                  "",
                  "/QK=3/L=7/C=3/H=",
                  13, old_menu, 0);

    menu_set_item(menu, ++mcnt,         // 6
                  "Install:",
                  "",
                  "/L=2/C=19/H=",
                  10, 0, 0);

    for (pkg_cnt = 1; pkg_cnt < MAXPKG; ++pkg_cnt) {
      cur_pkg = 1 << (pkg_cnt - 1);
      menu_str = copy(pkg_str, (pkg_cnt * 8) + 1, 6);
      if ((pkg_avail & cur_pkg)) {
        int qkey = 1;
        switch (pkg_cnt) {
          case 3:                       // SAP
          case 5:                       // TLIB4
            qkey = 2;
            break;
        }
        menu_set_item(menu, ++mcnt,     // 7 - 12
                      menu_str,
                      "",
                      "/C=20/H=/L=" + str(2 + pkg_cnt) + "/QK=" + str(qkey),
                      13, (vcs_pkgs & cur_pkg) == cur_pkg, 0);
      }
      else {
        menu_set_item(menu, ++mcnt,     // 7 - 12
                      " [ ] " + menu_str,
                      "",
                      "/C=20/H=/L=" + str(2 + pkg_cnt) +
                              "/ATTR=" + str(m_k_color),
                      10, 0, 0);
      }
    }

    menu_set_item(menu, ++mcnt,         // 13
                  "Default:",
                  "",
                  "/L=2/C=32/H=",
                  10, 0, 0);

    for (pkg_cnt = 0; pkg_cnt < MAXPKG; ++pkg_cnt) {
      menu_str = copy(pkg_str, (pkg_cnt * 8) + 1, 6);
      menu_set_item(menu, ++mcnt,       // 14 - 20
                    str(pkg_cnt) + " " + menu_str,
                    "",
                    "/G=@DEF_PKG@/QK=1/C=33/H=/L=" + str(3 + pkg_cnt) + "/R=" +
                        str(pkg_cnt),
                    12, def_pkg == pkg_cnt, 0);
    }


    if (net) {
      menu_set_item(menu, ++mcnt,       // 21
                    "Network:",
                    "",
                    "/L=9/C=2/H=",
                    10, 0, 0);

      menu_set_item(menu, ++mcnt,       // 22
                    "Master    ",
                    "",
                    "/QK=1/L=10/C=3/H=",
                    13, master, 0);

      menu_set_item(menu, ++mcnt,       // 23
                    "All Users ",
                    "",
                    "/QK=7/L=11/C=3/H=",
                    13, all_users, 0);
    }

    while (TRUE) {
      return_int = menu;

      rm("USERIN^DATA_IN /T=VCS INSTALLATION/HN=1/W=49/H=/#=" + str(mcnt));
      if (!return_int) {
        break;
      }
      def_pkg = global_int("@DEF_PKG@");
      set_global_int("@DEF_PKG@", 0);
      vcs_pkgs = 0;
      vcs_pkgs |= menu_item_int(menu, 7, 2) * PVCS;
      vcs_pkgs |= menu_item_int(menu, 8, 2) * RCS;
      vcs_pkgs |= menu_item_int(menu, 9, 2) * SAP;
      vcs_pkgs |= menu_item_int(menu, 10, 2) * SS;
      vcs_pkgs |= menu_item_int(menu, 11, 2) * TLIB4;
      vcs_pkgs |= menu_item_int(menu, 12, 2) * TLIB5;
      if (vcs_pkgs) {
        cur_pkg = 1 << (def_pkg - 1);
        if (!def_pkg || (vcs_pkgs & cur_pkg)) {
          break;
        }
        beep;
        make_message("Default package must be installed.");
        continue;
      }
      beep;
      make_message("At least one VCS package must be installed.");
    }
  }
  if (return_int) {                     // Continue installation
		ins_type = menu_item_int(menu, 3, 2);
		if (net) {
			master = menu_item_int(menu, 22, 2);
			all_users = menu_item_int(menu, 23, 2);
		}
		if (!ins_type) {                    // New
			old_path = me_path;
		}
		else {                              // Update
			old_menu = menu_item_int(menu, 5, 2);
      while (TRUE) {
        return_str = get_environment("ME_CFG_DIR");                     // tmj
        if(return_str == "")                                            // tmj
          return_str = get_environment("ME_USERS_DIR");                 // tmj
        if(return_str == "")                                            // tmj
          return_str = me_path;                                        // tmj
        rm("userin^queryBox /L=5/W=60/ML=80/HISTORY=VCS_WRK_HISTORY/T=" +
              "Input path to previous version files or <ESC> to abort");
        if (return_int) {
          return_str = fexpand(return_str);
          if (copy(return_str, length(return_str),1) != "\\") {
            return_str = return_str + "\\";
          }
          if (file_exists(copy(return_str,1,length(return_str) - 1)) != 2) {  // tmj
            rm("meError^messageBox /B=1/T=ERROR/M=" +
                    "Path not found.");
            continue;
          }
					old_path = return_str;
					if (net) {
            return_str += user_id + ".USR\\";
					}
          if (!(return_int = file_exists(return_str + "VCS.DB"))) {
            vcs_message = "No VCS.DB found in " + return_str;
            break;
          }
					switch_win_id(vcs_ref_win);
					file_name = return_str + "VCS.DB";
					call do_load_file;
					file_name = "VCS_REF.DB";
        }
        break;
      }
		}
		if (return_int) {                   // Do install
			switch_win_id(vcs_db_win);
    	file_name = usr_vcs_db;
			call rm_vcs_pkg;
			if (ins_type) {                   // Update install
				call update_file;
				call update_menu;
			}
			else {                            // New install
				call do_vcs_cfg;
			}
    	call do_save_file;
 			call del_config;
			erase_window;
			call ins_vcs;
			call set_def_pkg;
    	if (net) {
				userid_only = TRUE;
        user_id = "";
        set_user_path(me_path);
				file_name = usr_vcs_db;
				call do_load_file;
      	if (master) {
        	file_name = me_path + "VCS.DB";
					call update_file;
        	call do_save_file;
					call ins_vcs;
					call del_config;
      	}
      	if (all_users) {
					if (!first_file(old_path + "*.USR")) {
						do {
							if (last_file_attr & 0x10) {
								user_id = truncate_extension(str_del(last_file_name,
																										svl(old_path) + 1, 7));
        				set_user_path(old_path + copy(user_id, 1, 8) + ".usr");
								if (user_id == suser_id) {
									continue;
								}
								return_str = "";
								rm("makeUserPath");
                file_name = old_path + last_file_name + "\\VCS.DB";
								call update_file;
								call do_save_file;
								call ins_vcs;
								call del_config;
							}
						} while (!next_file);
					}
      	}
    	}
      return_int = TRUE;
    }
  }
  if (return_int) {
		make_message("VCS Installation complete.");
	}
  else {
    beep;
    make_message(vcs_message);
  }
  goto exit;

do_load_file:
  make_message("Loading " + file_name);
	error_level = 0;
	load_file(file_name);
	if (error_level) {
    return_str = "Error loading the configuration file: " + file_name +
        ".  Unpredictable results could occur.";
		beep;
		make_message(return_str);
		goto exit;
	}
  ret;

do_save_file:

	int t_backups = backups;
	str t_backup_path[80] = backup_path,
			t_temp_path[80] = temp_path;

  make_message("Saving " + file_name);

	backups = TRUE;
	backup_path = "";
	temp_path = "";

	Error_Level = 0;
	Save_File;
	backups = t_backups;
	backup_path = t_backup_path;
	temp_path = t_temp_path;
	if (error_level) {
    return_str = "Error " + str(error_level) + " saving the file: " + file_name +
        ".  Unpredictable results could occur.";
		beep;
		make_message(return_str);
		goto exit;
	}
  ret;

set_pkgs:
	vcs_pkgs = 0;
	tof;
	for (pkg_cnt = 1; pkg_cnt < MAXPKG; ++pkg_cnt) {
		cur_pkg = 1 << (pkg_cnt - 1);
		menu_str = remove_space(copy(pkg_str, (pkg_cnt * 8) + 1, 8));
		if (search_fwd("ID=" + menu_str, 0)) {
			vcs_pkgs |= cur_pkg;
		}
	}
	ret;

get_dflt:
	def_pkg = 0;
	tof;
	if (search_fwd("\fVCS.CFG", 0)) {
		tline = get_line;
		menu_str = parse_str("/VCS=", tline);
		if (return_int = xpos(menu_str, pkg_str, 1)) {
			def_pkg = return_int / 8;
		}
	}
	ret;

rm_vcs_pkg:
	tof;
	for (pkg_cnt = 1; pkg_cnt < MAXPKG; ++pkg_cnt) {
		cur_pkg = 1 << (pkg_cnt - 1);
		menu_str = remove_space(copy(pkg_str, (pkg_cnt * 8) + 1, 8));
		if (!(vcs_pkgs & cur_pkg)) {
			if (search_fwd("ID=" + menu_str, 0)) {
				make_message("Removing " + menu_str + " support.");
				del_line;
			}
		}
	}
	ret;

do_vcs_cfg:
	tof;
	for (pkg_cnt = 1; pkg_cnt < MAXPKG; ++pkg_cnt) {
		cur_pkg = 1 << (pkg_cnt - 1);
		menu_str = remove_space(copy(pkg_str, (pkg_cnt * 8) + 1, 8));
		if ((vcs_pkgs & cur_pkg)) {
      make_message("Configuring " + menu_str + " support...");
      rm("mehelp /IM=1/X=10/Y=3/W=60/LK=VCS_INST^" + menu_str + "/L=" +
							str(height));
      if (return_int) {
        if (pkg_cnt > 3) {                // SS, TLIB4, TLIB5
          if (search_fwd("ID=" + menu_str, 0)) {
            vcs_userid = user_id;
            tline = get_line;
            replace_parm(tline, "USERID=", vcs_userid);
            put_line(tline);
          }
        }
        rm("db /F=VCS.DB/DPT=VCS.CFG/DT=" + menu_str + " SETUP/NL=1/FV=" +
            menu_str);
      }
		}
	}
	ret;

update_file:
	tof;
	for (pkg_cnt = 1; pkg_cnt < MAXPKG; ++pkg_cnt) {
		menu_str = remove_space(copy(pkg_str, (pkg_cnt * 8) + 1, 8));
    make_message("Updating " + menu_str + " support...");
    switch_win_id(vcs_ref_win);
		if (search_fwd("ID=" + menu_str, 0)) {
			tline = get_line;
			if (!ins_type) {                   // New
				vcs_userid = user_id;
			}
			else
				vcs_userid = parse_str("USERID=", tline);
			if (!userid_only) {
				vcs_path = parse_str("PATH=", tline);
				vcs_ext = parse_str("LIBEXT=", tline);
				vcs_lok = parse_str("LOKEXT=", tline);
				vcs_dsrc = parse_int("DSRC=", tline);
			}
		}
		switch_win_id(vcs_db_win);
		if (search_fwd("ID=" + menu_str, 0)) {
			tline = get_line;
			if (pkg_cnt > 3) {              // SS, TLIB4, TLIB5
				replace_parm(tline, "USERID=", vcs_userid);
			}
			if (!userid_only) {
				replace_parm(tline, "PATH=", vcs_path);
				replace_parm(tline, "LIBEXT=", vcs_ext);
				if (pkg_cnt > 4) {
					replace_parm(tline, "LOKEXT=", vcs_lok);
				}
				replace_parm(tline, "DSRC=", str(vcs_dsrc));
			}
			put_line(tline);
		}
	}
	ret;

update_menu:
	if (old_menu) {
		make_message("Adding old menu items...");
		if (switch_win_id(vcs_ref_win)) {
			tof;
			if (search_fwd("\fVCS.MNU", 0)) {
				down;
				mcnt = c_line;
				wrk_win = cur_window;
			}
			switch_win_id(vcs_db_win);
			tof;
			insert_mode = TRUE;
			if (search_fwd("\fVCS.MNU", 0)) {
				down;
				search_fwd("\f", 0);
				goto_col(1);
				cr;
				up;
				text("NAME=...DividerTYPE=6");
				tline = get_line_from_win(mcnt++, wrk_win);
				while (xpos("NAME=", tline, 1)) {
					cr;
					text(tline);
					tline = get_line_from_win(mcnt++, wrk_win);
				}
				if (global_int("~VCS_MENU")) {
					menu_delete(global_int("~VCS_MENU"));
					set_global_int("~VCS_MENU", 0);
				}
			}
		}
	}
	ret;

ins_vcs:
	rm("vcs^vcs_save");
	rm("vcs^vcs_add_to_user");
	rm("vcs^vcs_keys /K=" + str(keyval));
	keyval = return_int;
	ret;

set_def_pkg:
	set_global_str("!VCS", "");
	rm("vcs^vcs_restore");
	push_key(13, 28);                   	// <ENTER>
	push_key(13, 28);                   	// <ENTER>
	switch (def_pkg) {
		case 0:
			push_key(78, 49);              		// NP
			break;

		case 1:
			push_key(80, 25);              		// P
			break;

		case 2:
			push_key(82, 19);              		// R
			break;

		case 3:
			push_key(83, 31);              		// S

		case 4:
			push_key(83, 31);              		// S
			break;

		case 5:
			push_key(84, 20);              		// T
			break;

		case 6:
			push_key(0, 79);               		// <END>
	}
	push_key(32,57);                   		// Space
	rm("vcs^vcs_configure");
	push_key(9, 15);                   		// <TAB>
	push_key(9, 15);                   		// <TAB>
	rm("vcs^vcs_configure");
	ret;

del_config:
	return_str = "MECONFIG.DB";
	rm("makeUserPath");
	if (switch_file(return_str)) {
		if (file_changed) {
			call do_save_file;
		}
		delete_window;
	}
	switch_win_id(vcs_db_win);
	ret;

exit:
  rm("VCS_STATUS");

  menu_delete(menu);
	if (switch_win_id(vcs_ref_win)) {
		delete_window;
	}
	if (switch_win_id(vcs_db_win)) {
		delete_window;
	}
  switch_win_id(org_win);
	insert_mode = sim;
	set_user_path(suser_path);
  user_id = suser_id;
	ignore_case = sic;
	reg_exp_stat = sre;
  refresh = srf;
}

void replace_parm(str &src, fnd, repl) trans2 {

	if (return_int = xpos(fnd, src, 1)) {
		var_remove_str(fnd, src);
	}
	else {
		return_int = svl(src) + 1;
	}
 	src = str_ins(fnd + repl, src, return_int);
}
