prototype mesys
{
  void SETSCRN(int set_icon_rows, int t_icon_rows, int ignore_icons, str win_parms,
              int set_min_window_row, int t_min_window_row, int delete_flag);
	void cursor_to_line( int l );
	str GetUserMacroPath();
	str GetUserPath( int create_dirs );
}
