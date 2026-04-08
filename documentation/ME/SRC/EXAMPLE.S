/*******************************************************************************
														MULTI-EDIT MACRO FILE

Name:	EXAMPLE

Description: Contains examples of how to call the Multi-Edit user interface
macros.

EXAMPLE				- Example of how to use the macro TOP_MENU to create a top level
								menu.
SUB_EXAMPLE1	- Example of how to use the macro SUB_MENU to create a sub level
								menu.
BAR_EXAMPLE		- Example of the use of the macro XMENU to create a bar menu.
VER_EXAMPLE		- Example of the use of the macro XMENU to create a vertical menu.
VAR_EXAMPLE		- Example of how to create a variable length menu (list box).
SUB_EXAMPLE2	- Another SUB_MENU example.
QUE_EXAMPLE		- Example of how to use the macro QUERYBOX.
DATA_IN_EXAMPLE		- Example of how to create a dialog box.
EXAMPLE_F_PROMPT	- Creates a dialog box containing a string input and a
										directory box.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

macro EXAMPLE {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	EXAMPLE

Description:  Example of how to use the macro TOP_MENU to create a top level
							menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int menu = menu_create;

	menu_set_item(menu,1,"Example-1","","/H=FIXEDMENU/M=SUB_EXAMPLE1",0,0,0);
	menu_set_item(menu,2,"Example-2","","/H=FIXEDMENU/M=SUB_EXAMPLE2",0,0,0);
/* Notice the lack of a /M= parameter in this one.  This will not run a
 macro, however, you can determine if the user chose this item if you check
 to see if Return_Int = 3 after running TOPMENU */
	menu_set_item(menu,3,"Exit-menu","","/H=FIXEDMENU/S=2",0,0,0);

	Return_Int = menu;
	RM("USERIN^TOPMENU /HN=1/#=3/X=1/Y=2/S=1/BC=1/L=TOP_MENU EXAMPLE");

	menu_delete(menu);
	return_int = 100;
}

macro SUB_EXAMPLE1 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	SUB_EXAMPLE1

Description:  Example of how to use the macro SUB_MENU to create a sub level
							menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int menu = menu_create;

	menu_set_item(menu,1,"XMENU horizontal example","","/H=FIXEDMENU/M=BAR_EXAMPLE",0,0,0);
	menu_set_item(menu,2,"XMENU Vertical example","","/H=FIXEDMENU/M=VER_EXAMPLE",0,0,0);
	menu_set_item(menu,3,"List box example","","/H=VARMENU/M=VAR_EXAMPLE",0,0,0);

	Return_Int = menu;
	RM("USERIN^SUBMENU /HN=1/#=3/S=1/L=SUB_MENU EXAMPLE 1/X=" +
		Parse_Str("/X=",MParm_Str) + "/Y=" + Parse_Str("/Y=",MParm_Str));

	menu_delete(menu);
}

macro BAR_EXAMPLE {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	BAR_EXAMPLE

Description: Example of the use of the macro XMENU to create a bar menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int kill_count = Parse_Int("/BC=", mparm_str);

	RM("USERIN^XMENU /B=1/S=1/M=Choice1(FIXEDMENU)Choice2()Choice3()/X=" +
		Parse_Str("/X=",MParm_Str) + "/Y=" +
		Parse_Str("/Y=",MParm_Str));
	if (Return_Int > 0) {
/* If an item was selected then kill of all of the other menus and then put
	 up a message */
		while (Box_Count > Kill_Count) {
			Kill_Box;
		}
		Make_Message("XMENU HORIZONTAL: Item " + str(Return_Int) + " was selected.");
	}
}

macro VER_EXAMPLE {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	VER_EXAMPLE

Description: Example of the use of the macro XMENU to create a vertical menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int kill_count = Parse_Int("/BC=", mparm_str);

	RM("USERIN^XMENU /T=1/B=1/S=1/M=Choice1(FIXEDMENU)Choice2()Choice3()/X=" +
		Parse_Str("/X=",MParm_Str) + "/Y=" +
		Parse_Str("/Y=",MParm_Str));
/* Return_Int will now be < 1 if <ESC> was pressed, or it will equal the number
	 of the menu item that was selected */
	if (Return_Int > 0) {
		while (Box_Count > Kill_Count) {
			Kill_Box;
		}
		Make_Message("XMENU VERTICAL: Item " + str(Return_Int) + " was selected.");
	}
}

macro VAR_EXAMPLE {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	VAR_EXAMPLE

Description: Example of how to create a variable length menu (list box).

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Active_Window = Window_Id,
/* It is always good programming practice to store system variables that you
alter into temporary variables so that they can be restored upon exit */
			T_Insert_Mode = Insert_Mode,
			T_Refresh = Refresh,
			menu = menu_create
			;
	char	X = Parse_Str("/X=",MParm_Str),
				Y = Parse_Str("/Y=",MParm_Str);

/* Create a new window to place the menu choices in */
	Refresh = False;
	Create_Window;
	Put_Line("Choice1");
	Down;
	Put_Line("Choice2");
	Down;
	Put_Line("Choice3");
	Tof;
/* Here, we are creating user options by creating these menu items */
MENU_LOOP:
	menu_set_item(menu,1,"Create","","/KC=<Ins>/W=11/K1=0/K2=82/R=3/L=1/C=4",11,0,0);
	menu_set_item(menu,2,"Delete","","/KC=<Del>/W=11/K1=0/K2=83/R=4/L=1/C=17",11,0,0);
	menu_set_item(menu,3,"","","/L=2/C=9/DC=1/WIN=" + str(cur_window) +	"/OR=" + str( c_row ),15,0,0);

	Return_Int = menu;
	RM("USERIN^DATA_IN /HN=1/A=2/PRE=V/#=3/DBL=1/S=3/H=VARMENU/X=" + x + "/Y=" + y +	"/OR=" + Str(C_Row));

	if (Return_Int == 1) {
		Return_Int = C_Line;
	} else if (Return_Int == 3) {
		Return_Str = "";
/* This is the prompt for the new menu item to add */
		RM("USERIN^QUERYBOX /H=PROMPTS/W=7/P=Enter a new menu choice:/T=CREATE/C=" +
			X + "/L=" + Y);

		if ((Return_Int != 0) && (Return_Str != "")) {
			Eol;
			Insert_Mode = True;
			Cr;
			Put_Line(Return_Str);
		}
		Goto MENU_LOOP;
	} else if (Return_Int == 4) {
/* This is verification that the user really wants to delete this item */
		RM("USERIN^VERIFY /H=PROMPTS/BL=CONFIRM/C=" + X + "/L=" + Y + "/T=Delete \""
				+ Get_Line + "\"?");
		if (Return_Int) {
			Del_Line;
		}
		Goto MENU_LOOP;
	}

	menu_delete(menu);
	Delete_Window;
	Switch_Win_Id(Active_Window);
	Insert_Mode = T_Insert_Mode;
	Refresh = T_Refresh;
}

macro SUB_EXAMPLE2 {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	SUB_EXAMPLE2

Description:  Example of how to use the macro SUB_MENU to create a sub level
							menu.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int	menu = menu_create;

	menu_set_item(menu,1,"QUERY_BOX example","","/H=FIXEDMENU/M=QUE_EXAMPLE",0,0,0);
	menu_set_item(menu,2,"Dialog box example","","/H=DATABOX/M=DATA_IN_EXAMPLE",0,0,0);

	Return_Int = menu;
	RM("USERIN^SUBMENU /HN=1/#=2/S=1/L=SUB_MENU EXAMPLE 2/X=" +
		Parse_Str("/X=",MParm_Str) + "/Y=" + Parse_Str("/Y=",MParm_Str));

	menu_delete(menu);

}

macro QUE_EXAMPLE {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	QUE_EXAMPLE

Description: Example of how to use the macro QUERYBOX.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int	kill_count,
			kill_count = Parse_Int("/BC=", mparm_str);

	Return_Str = "EXAMPLE";
/* Notice we are enabling the history list here with /HISTORY= */
	RM("USERIN^QUERYBOX /HISTORY=QUE_PASA/H=PROMPTS/C=" +
		Parse_Str("/X=",MParm_Str) +
	"/L=" + Parse_Str("/Y=",MParm_Str) +
	"/W=10/P=Enter whatever turns you on:/T=QUERYBOX EXAMPLE");

/* If the input was accepted, then kill of all of the other menus and then put up
a message */
	if (Return_Int > 0) {
		while (Box_Count > Kill_Count) {
			Kill_Box;
		}
		Make_Message("The QUERYBOX was accepted");
	}
}

macro Data_In_Example {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	DATA_IN_EXAMPLE

Description: Example of how to create a dialog box.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	int
					original_window = cur_window,    /* remember where we were */
					T_Int,                           /* temp integer variable  */
					menu = menu_create               /* create menu data structure */
					;
	str
					T_Str                             /*  temp string variable  */
					;

	refresh = FALSE;                 /*  turn off screen updating  */
	switch_window( window_count );    /*  go to the end of the window list  */
	create_window;                    /*  create a window to use for our example
																			list field */
																		/*  Put some junk in our list  */
	while(  (c_line <= 20)  ) {
		put_line( str( c_line ) + " -- item -- " + str( c_line ) );
		down;
	}
/* Move to line 4.  This will become the initial selection for this list.  */
	Goto_Line(4);

/* Now we will start creating some fields */
	menu_set_item(menu,1,"String input: ","test text","/QK=1/L=1/C=1/W=15/ML=50/HISTORY=EXAMPLE",0,0,0);
	menu_set_item(menu,2,"Numeric input (1-999):","","/QK=1/L=2/C=1/W=3/MIN=1/MAX=999",1, 1,0);

/* Notice the () to delimit the choices */
	menu_set_item(menu,3,"Multiple choice:","choice A()choice B()choice C()choice D()","/QK=1/L=1/C=44/W=10",3, 1 ,0);

/* Notice if the value of the 2nd integer is 1, the /T= (True) parameter
will be displayed, and if 0, /F= (False). */
	menu_set_item(menu,4,"Toggle field:   ", "/T=Yes/F=No" ,"/QK=1/L=2/C=44/W=3",5, 1 ,0);

/* Setup a set of radio buttons.  The first field will be a static data field to
act as the label.  Notice only one of the 2nd integers are initialized to 1.
Notice also the /G= parameter.  This is the name of the global integer where
the value corresponding to the selected button's /R= parameter will be stored. */
	menu_set_item(menu,5,"Radio buttons:","","/QK=1/L=4/C=1",10,0,0);
	menu_set_item(menu,6,"button 1  ","","/L=5/C=2/G=@RADIO@/R=1",12, 1 ,0);
	menu_set_item(menu,7,"button 2  ","","/L=6/C=2/G=@RADIO@/R=2",12, 0 ,0);
	menu_set_item(menu,8,"button 3  ","","/L=7/C=2/G=@RADIO@/R=3",12, 0 ,0);

/* Setup a set of check boxes.  The first field will be a static data field to
act as the label.  The 2nd integers with a non zero value will have an "X"
in them. */
	menu_set_item(menu,9,"Check boxes:","","/QK=1/L=4/C=20",10,0,0);
	menu_set_item(menu,10,"box 1  ","","/L=5/C=21",13, 1 ,0);
	menu_set_item(menu,11,"box 2  ","","/L=6/C=21",13, 0 ,0);
	menu_set_item(menu,12,"box 3  ","","/L=7/C=21",13, 1 ,0);

/* Now create the sample list field.  We have already created a list window. */
	menu_set_item(menu,13,"List field","","/QK=1/L=4/C=44/W=20/HT=6/WIN=" + str(CUR_WINDOW),15,0,0);

/* Here we will use a run macro (type 8) field to run our example file prompt
macro and select a file name.  You can experiment with changing the directory
mask in the 2nd string in this menu item */
	menu_set_item(menu,14,"select a Filename:", "*.*","/QK=10/C=1/L=10/W=20/M=EXAMPLE_F_PROMPT",8,0,0);

	Return_Int = menu;
	RM("USERIN^DATA_IN /HN=1/PRE=EX/#=14/T=EXAMPLE DIALOG BOX/H=DATABOX");

/* Here is some examples of how the different types of data can now be retrieved */

	T_Str = menu_item_str(menu,1,2); /*  String type 0  */

	T_Int = menu_item_int(menu,2,2); /*  Integer type 1  */

	T_Int = menu_item_Int(menu,4,2); /*  Toggle field.  The value will be either
																		 0 or 1.  */

	T_Int = Global_Int("@RADIO@"); /*  Radio button group.  Notice the name of
																			this global */

	T_Int = menu_item_int(menu,10,2); /*  Check box.  Same as toggle field  */

	T_Int = C_Line;   /* For the list field, you can either get an integer... */
	T_Str = Get_Line; /* or a string. */

	T_Str = menu_item_str(menu,14,2); /* Run macro string type 8 */

	delete_window;                      /* kill our list window  */

	switch_window( original_window );   /* Go back to where we where */

	menu_delete(menu);            /* get rid of the menu data structure */
}

macro Example_F_Prompt {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:  Example_F_Prompt

Description:  Creates a dialog box containing a string input and a directory
							box.
Returns:	RETURN_INT = 1 if OK was selected,
					RETURN_INT = 0 if Cancel was selected.

					RETURN_STR = the filename that was chosen.

Note that you could accomplish this macro exactly with:

RM("MEUTIL1^FILE_PROMPT /T=Filename prompt with directory");

we are not doing this, so that we may illustrate how to use the
Directory Field type.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
		int menu = menu_create,
				active_window = window_id; /* save current window id so we can switch
																			back*/

		switch_window(window_count);
		create_window; /* create a window for the drive letter and directory list */

/* This is the string input field for the filename.  The field before a
directory field(type 20) MUST ALWAYS be a string input field(type 0).  Notice
we initialize the 2nd string in menu item 1 to return_str which contains the
value of the field that called this macro. */
		menu_set_item( menu, 1, "Name:", return_str,
			 "/QK=1/C=1/W=51/ML=80/HISTORY=FILE_HISTORY",0,0, 0);

/* This is the file list field */
		menu_set_item( menu, 2, "", "", "/C=1/L=3/SDD=1",20,0, 0);

/* This is the drive/directory field */
		menu_set_item( menu, 3, "Directories", "", "/OL=1/DC=1/QK=1/C=34/L=3/W=14/HT=14/WIN=" + str(cur_window),15,0,0);

/* These are button fields */
		menu_set_item( menu, 4, "OK","", "/KC=<ENTER>/C=51/L=4/K1=13/K2=28/R=1",11,0,0);
		menu_set_item( menu, 5, "Cancel","", "/KC=<ESC>/C=51/L=6/K1=27/K2=1/R=0",11,0,0);
		menu_set_item( menu, 6, "Help","","/KC=<F1>/C=51/L=10/K1=0/K2=59/R=2/QK=1",11,0,0);
		menu_set_item( menu, 7, "Sort", "DIRSHELL^DIRSORT /UPDATE=5","/C=51/L=12/M=1/QK=1",11,0,0);

		RETURN_INT = menu;
		RM("UserIn^Data_In /HN=1/T=" + parse_str("/T=", mparm_str) +
				"/S=1/#=7/POSG=@FPPOS@/H=" + parse_str("/H=", mparm_str)
				 );
/* Place the value of this field into Return_Str so it will become the value
of the field that called this macro */
		return_str = menu_item_str( menu, 1, 2 );;

		menu_delete(menu);
		delete_window;
		switch_win_id(active_window);
}
