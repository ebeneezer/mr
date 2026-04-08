macro_file STATSET;
/*******************************************************************************
														MULTI_EDIT MACRO FILE

Name: STATSET

STATSET - Provides an alternate screen setup.
STATMAC - Writes the template for the status line to the screen.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

macro STATSET {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: STATSET

Description: This is an alternate screen setup.  Set this up in the
							Install-Screen setup menu.

Parameters:  /S=1 will put the status line on the bottom of the screen.
						 /S=0 will put the status line on the top of the screen.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int jx;

	refresh = false;
	status_str = 'Multi-Edit V' + version + copy('        ', 1, 8 - length(version)) + '                                    L      C';
	mode_col = 0;
	insert_col = 0;
	mem_col = 0;
	caps_col = 0;
	record_col = 0;
	time_col = 0;
	line_col = 58;
	col_col = 65;
	if(  parse_int('/S=',mparm_str)  ) {
		status_row = screen_length;
	} else {
		status_row = 1;
	}
	message_row = status_row;
	if(  menu_bar_row != 0  ) {
		menu_bar_row = status_row + 1;
	}
	status_macro('STATMAC');
	message_col = 1;
	message_length = 55;
	working_col = 69;
}

macro STATMAC {
/*******************************************************************************
																MULTI_EDIT MACRO

Name: STATMAC

Description: Writes the template for the status line to the screen.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
		set_virtual_display;
		if(  mode == 0  ) {
			if(  global_int('MENU_LEVEL') != 0  ) {
				write( 'MENU/PROMPT ', 69, status_row, 0, stat2_color );
			} else if(  (cur_window > 0)  ) {
				write( '            ', 69, status_row, 0, stat2_color );
				write( truncate_path(file_name), 69, status_row, 0, stat2_color );
			}
		} else if(  mode == DOS_SHELL  ) {
			write( ' DOS SHELL  ', 69, status_row, 0, stat2_color );
		}
		update_virtual_display;
		reset_virtual_display;
}
