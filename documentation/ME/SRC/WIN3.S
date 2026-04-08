macro_file win3;


/*---------------12-30-92 10:50am-------------------
 win3_clipboard
 op = The clipboard operation to perform.
			Use /M= from command line.
			0 = paste text from clipboard
			1 = copy text to clipboard
--------------------------------------------------*/
void win3_clipboard( int op = parse_int("/M=",mparm_str) )
{
	working;
	int result = 0;
	switch( op )
	{
		case 0 :
			result = win3_paste;
			if(!global_int("CURSOR_EOB"))
			{
				rm("TOPBLOCK");
			}
			break;
		case 1 :
			result = win3_copy;
			break;
	}
	if(result == 0)
	{

	}
	else if(result == 10)
	{
		error_level = 4001;
	}
	else if(result < 4)
	{
		error_level = 4000;
	}
	else
	{
		error_level = 4002;
	}
	redraw;
}

// Commands from Windows

#define ME_EXIT 1

// Tsr Commands
#define ME_DOSQUERY   1
#define ME_WINQUERY   2
#define ME_DOSCMD     3
#define ME_WINCMD     4

/*---------------06-25-93 05:37pm-------------------
 Setup for Smart Exit of Dos App in windows

 Name:  SmartExit_Setup

 Entry: None

 Exit:  None

--------------------------------------------------*/
void SmartExit_Setup () Dump
{
  Int Timer_Handle = Set_Timer_Event ('Check_Windows', 1, $FF);

}
void Check_Windows () No_Break
{
  R_AX = $eded;
  R_BX = ME_DOSQUERY;

  Intr ($2F);

  switch (R_AX)
  {
  case 1:
    rm ('exit^exit /NE=1');

    R_AX = $eded;
    R_BX = ME_WINCMD;
    if (Return_Int)
    {
      R_CX = 1;
      Intr ($2f);
      Quit (0);
    }
    else
    {
      R_CX = 2;
      Intr ($2f);
    }
  }
}
